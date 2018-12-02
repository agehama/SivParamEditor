#include <Siv3D.hpp> // OpenSiv3D v0.3.0
#include "ParamEditor.hpp"

using namespace pmt;
using namespace pmt::detailImpl;

class ParameterReceiver
{
public:
	static void Update()
	{
		auto& i = instance();
		i.reportUpdate = true;

		for (const auto& keyVal : i.state.receivedBuffer.colors)
		{
			if (!i.state.editor.exists(keyVal.first))
			{
				i.state.editor.add(keyVal.first, keyVal.second);
			}
		}

		i.state.editor.update();
		AddData(i.state.editor.getUpdates());
	}

	static ParameterData& ReceivedBuffer()
	{
		auto& i = instance();
		return i.state.receivedBuffer;
	}

	static void AddData(const std::unordered_map<String, Color>& colors)
	{
		auto& i = instance();
		for (const auto& color : colors)
		{
			i.sendBuffer.colors[color.first] = color.second;
		}
	}

private:
	static void ReceiveNewColors()
	{
		auto& i = instance();
		i.server.startAccept(PortNumber);
		bool connected = false;

		while (!i.terminationRequest)
		{
			if (!i.reportUpdate)
			{
				continue;
			}
			i.reportUpdate = false;

			switch (i.phase)
			{
			case ParameterReceiver::Error:
			{
				Window::SetTitle(U"初期化に失敗");
				break;
			}
			case ParameterReceiver::Ready:
			{
				if (i.server.hasSession())
				{
					connected = true;

					//Window::SetTitle(U"TCPServer: 接続完了！");

					Array<Byte> byteArray(4 * 256);
					if (!i.server.read(byteArray.data(), 4 * 256, unspecified))
					{
						break;
					}

					Deserializer<ByteArray> filePathDeserializer(byteArray);

					std::array<char32_t, 256> filePath{};
					ParameterData receivedData;
					filePathDeserializer(filePath);

					for (const auto& keyVal : receivedData.colors)
					{
						i.state.receivedBuffer.colors[keyVal.first] = keyVal.second;
					}

					i.directoryPath = String(filePath.data());
					i.phase = WaitingClient;
					i.directoryWatcher = DirectoryWatcher(i.directoryPath);

					const auto sendFilePath = i.directoryPath + U"receive.dat";
					{
						Serializer<BinaryWriter> serializer(sendFilePath);
						serializer(detailImpl::EditorVersion);
					}

					//クライアントはサーバーと通信を行うよりも前に send.dat にバージョンを記録しているのでここで読めるはず
					const auto receiveFilePath = i.directoryPath + U"send.dat";
					if (!FileSystem::IsEmpty(receiveFilePath))
					{
						{
							Deserializer<BinaryReader> deserializer(receiveFilePath);

							unsigned version;
							deserializer(version);

							if (version != detailImpl::EditorVersion)
							{
								i.phase = Error;
								break;
							}
						}

						BinaryWriter writer(receiveFilePath);
					}
					else
					{
						i.phase = Error;
						break;
					}

					const auto saveFilePath = i.directoryPath + U"save.dat";
					if (FileSystem::Exists(saveFilePath) && !FileSystem::IsEmpty(saveFilePath))
					{
						Deserializer<BinaryReader> deserializer(saveFilePath);
						deserializer(i.state);
					}

					break;
				}

				if (connected && !i.server.hasSession())
				{
					i.server.disconnect();
					connected = false;
					//Window::SetTitle(U"TCPServer: 再接続待機中...");
					i.server.startAccept(PortNumber);
				}

				break;
			}
			case ParameterReceiver::WaitingClient:
			{
				const auto sendFilePath = i.directoryPath + U"receive.dat";

				if (FileSystem::IsEmpty(sendFilePath))
				{
					i.phase = Running;
					i.stopwatch.start();

					i.server.disconnect();
				}

				break;
			}
			case ParameterReceiver::Running:
			{
				const auto receiveFilePath = i.directoryPath + U"send.dat";
				for (const auto& pathAction : i.directoryWatcher.retrieveChanges())
				{
					if (pathAction.first == receiveFilePath && !FileSystem::IsEmpty(receiveFilePath))
					{
						{
							Deserializer<BinaryReader> deserializer(receiveFilePath);

							ParameterData receivedData;
							deserializer(receivedData);

							for (const auto& keyVal : receivedData.colors)
							{
								i.state.receivedBuffer.colors[keyVal.first] = keyVal.second;
							}
						}

						BinaryWriter writer(receiveFilePath);
					}
				}

				const auto sendFilePath = i.directoryPath + U"receive.dat";
				if (!i.sendBuffer.colors.empty() && FileSystem::IsEmpty(sendFilePath))
				{
					try
					{
						Serializer<BinaryWriter> serializer(sendFilePath);
						serializer(i.sendBuffer);
						i.sendBuffer = ParameterData();
					}
					catch (std::exception& e)
					{
						Logger << Unicode::Widen(e.what());
					}
				}

				if (500 <= i.stopwatch.ms())
				{
					i.stopwatch.restart();
					const auto saveFilePath = i.directoryPath + U"save.dat";
					Serializer<BinaryWriter> serializer(saveFilePath);
					serializer(i.state);
				}

				break;
			}
			default: break;
			}
		}
	}

	ParameterReceiver()
	{
		worker = std::thread(ReceiveNewColors);
	}

	ParameterReceiver(const ParameterReceiver&) = delete;

	~ParameterReceiver()
	{
		terminateAllThreads();
	}

	static ParameterReceiver& instance()
	{
		static ParameterReceiver obj;
		return obj;
	}

	void terminateAllThreads()
	{
		terminationRequest = true;
		worker.join();
	}

	enum Phase { Error, Ready, WaitingClient, Running };

	String directoryPath;
	DirectoryWatcher directoryWatcher;

	TCPServer server;
	std::thread worker;
	ParameterData sendBuffer;

	ServerState state;

	bool terminationRequest = false;
	bool reportUpdate = false;

	Phase phase = Ready;
	Stopwatch stopwatch;
};

void Main()
{
	Window::Resize(1280, 720);
	Graphics::SetBackground(HSV(0.0, 0.0, 0.25));

	while (System::Update())
	{
		ParameterReceiver::Update();
	}
}
