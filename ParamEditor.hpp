#include <fstream>
#include <thread>
#include <mutex>

#include <Siv3D.hpp> // OpenSiv3D v0.3.0

#ifndef PMT_RELEASE_FLAG
#define PMT_RELEASE_FLAG false
#endif

namespace pmt
{
	namespace detailImpl
	{
		static constexpr uint16 PortNumber = 52823;
		static constexpr unsigned EditorVersion = 1;

		struct ParameterData
		{
			template <class Archive>
			void SIV3D_SERIALIZE(Archive& archive)
			{
				archive(colors);
			}

			std::unordered_map<String, ColorF> colors;
		};

		class ColorEditor
		{
		public:
			ColorEditor() = default;
			ColorEditor(const Color& color)
			{
				const HSV hsv(color);
				currentHuePos = 1.0 - hsv.h / 360.0;
				currentPos = Vec2(hsv.s, 1.0 - hsv.v);
			}

			void update()
			{
				const Vec2 satBoxTL = colorBoxTL + Vec2(colorBoxWidth + satBoxInterval, 0);

				{
					//明度と彩度の操作
					if (RectF(colorBoxTL, colorBoxWidth, colorBoxWidth).stretched(10).leftPressed())
					{
						currentPos = Saturate((Cursor::PosF() - colorBoxTL) / colorBoxWidth);
					}

					//色相の操作
					if (RectF(satBoxTL, satBoxWidth, colorBoxWidth).stretched(5).leftPressed())
					{
						currentHuePos = Saturate((Cursor::PosF().y - satBoxTL.y) / colorBoxWidth);
					}
				}
			}

			void draw()const
			{
				const Vec2 satBoxTL = colorBoxTL + Vec2(colorBoxWidth + satBoxInterval, 0);

				getScope().draw(Color(128, 128, 128));

				//左のボックスの描画
				{
					const double currentH = 360.0 - 360.0*currentHuePos;
					//RectF(colorBoxTL, colorBoxWidth, colorBoxWidth).drawFrame(3.0, Palette::Black);
					//RectF(colorBoxTL, colorBoxWidth, colorBoxWidth).draw({ HSV(currentH, 0.0, 1.0),HSV(currentH, 1.0, 1.0),HSV(currentH, 1.0, 0.0),HSV(currentH, 0.0, 0.0) });

					int divNum = 4;
					const int unitWidth = colorBoxWidth / divNum;
					for (int y = 0; y < divNum; ++y)
					{
						const double topV = 1.0 - 1.0*y / divNum;
						const double bottomV = 1.0 - 1.0*(y + 1) / divNum;
						for (int x = 0; x < divNum; ++x)
						{
							const double leftS = 1.0*x / divNum;
							const double rightS = 1.0*(x + 1) / divNum;
							RectF(colorBoxTL + Vec2(x, y)*unitWidth, unitWidth, unitWidth).draw({
								HSV(currentH, leftS, topV),HSV(currentH, rightS, topV),HSV(currentH, rightS, bottomV),HSV(currentH, leftS, bottomV)
								});
						}
					}

					const Color circleColor = currentPos.y < 0.3 ? Palette::Black : Palette::White;
					getCircle().drawFrame(1.0, circleColor);
				}

				//右のボックスの描画
				{
					const int divNum = 6;
					for (int i = 0; i < divNum; ++i)
					{
						const int unitHeight = colorBoxWidth / divNum;
						const Vec2 currentSatBoxTL = satBoxTL + Vec2(0, i*unitHeight);
						const double unitH = 360.0 / divNum;
						const Color topColor = HSV(360.0 - unitH * i, 1.0, 1.0);
						const Color bottomColor = HSV(360.0 - unitH * (i + 1), 1.0, 1.0);
						RectF(currentSatBoxTL, satBoxWidth, unitHeight).draw({ topColor,topColor,bottomColor,bottomColor });
					}

					const Vec2 satLineLeft = satBoxTL + Vec2(0, colorBoxWidth*currentHuePos);
					Line(satLineLeft, satLineLeft + Vec2(satBoxWidth, 0)).draw(1.0, Palette::Black);
				}
			}

			HSV getHSV()const
			{
				return HSV(360.0 - 360.0*currentHuePos, currentPos.x, 1.0 - currentPos.y);
			}

			RectF getScope()const
			{
				const double width = colorBoxWidth + satBoxInterval + satBoxWidth;
				return RectF(colorBoxTL, width, colorBoxWidth).stretched(1.0);
			}

			RectF getTabScope(const Vec2& offset = { 0, 0 })const
			{
				const Vec2 bottomLeft = getScope().tl() + offset;
				const double tabHeight = 25;
				return RectF(bottomLeft - Vec2(0, tabHeight), 70, tabHeight);
			}

			Vec2 colorBoxTL = Vec2(100.0, 100.0);

			template <class Archive>
			void SIV3D_SERIALIZE(Archive& archive)
			{
				archive(colorBoxTL, currentPos, currentHuePos, colorName);
			}

		private:
			Circle getCircle()const
			{
				return Circle(colorBoxTL + currentPos * colorBoxWidth, 10.0);
			}

			int colorBoxWidth = 300;

			int satBoxInterval = 10;
			int satBoxWidth = 30;

			Vec2 currentPos = Vec2(0.0, 0.0); //[0.0, 1.0]
			double currentHuePos = 0.0; //[0.0, 1.0]
			String colorName = U"Color";
		};

		class MultiColorEditors
		{
		public:
			struct WindowIndex
			{
				size_t groupIndex;
				size_t colorIndex;
				WindowIndex() = default;
				WindowIndex(size_t groupIndex, size_t colorIndex) :
					groupIndex(groupIndex),
					colorIndex(colorIndex)
				{}

				bool operator==(const WindowIndex& other)const
				{
					return groupIndex == other.groupIndex && colorIndex == other.colorIndex;
				}
			};

			void add(const String& name, const Color& color)
			{
				colors[name] = color;

				if (colorGroups.empty())
				{
					colorGroups.emplace_back();
					groupPositions.push_back(Vec2(100, 100));
				}
				colorGroups.back().push_back(name);
			}

			void update()
			{
				currentUpdates.clear();

				if (grabbingColor)
				{
					const auto& info = grabbingColor.value();

					const WindowIndex index = searchByName(info.name).value();

					//切り離された状態
					if (colorGroups[index.groupIndex].size() == 1)
					{
						groupPositions[index.groupIndex] = Cursor::PosF() - info.posOffset;

						for (size_t groupIndex = 0; groupIndex < colorGroups.size(); ++groupIndex)
						{
							if (groupIndex == index.groupIndex)
							{
								continue;
							}

							//既存のグループへのマージ
							if (getGroupOuterScope(groupIndex).mouseOver())
							{
								colorGroups[groupIndex].push_back(info.name);
								colorGroups.erase(colorGroups.begin() + index.groupIndex);
								groupPositions.erase(groupPositions.begin() + index.groupIndex);
								break;
							}
						}
					}
					//結合された状態
					else
					{
						//グループからの切り離し
						if (!getGroupOuterScope(index.groupIndex).mouseOver())
						{
							auto& currentGroup = colorGroups[index.groupIndex];
							for (size_t colorIndex = 0; colorIndex < currentGroup.size(); ++colorIndex)
							{
								if (info.name == currentGroup[colorIndex])
								{
									currentGroup.erase(currentGroup.begin() + colorIndex);
									break;
								}
							}
							colorGroups.emplace_back();
							colorGroups.back().push_back(info.name);
							groupPositions.push_back(Cursor::PosF() - info.posOffset);
						}
						//グループ内での並べ替え
						else
						{
							auto& currentGroup = colorGroups[index.groupIndex];
							for (size_t colorIndex = 0; colorIndex < currentGroup.size(); ++colorIndex)
							{
								if (getColorScope({ index.groupIndex, colorIndex }).mouseOver() && index.colorIndex != colorIndex)
								{
									std::iter_swap(currentGroup.begin() + colorIndex, currentGroup.begin() + index.colorIndex);
								}
							}
						}
					}

					if (MouseL.up())
					{
						grabbingColor = none;
					}
				}
				else if (grabbingGroup)
				{
					groupPositions[grabbingGroup.value()] += Cursor::DeltaF();
					if (MouseL.up())
					{
						grabbingGroup = none;
					}
				}
				else if (edittingColor)
				{
					auto& edit = edittingColor.value();
					edit.colorEditor.update();
					colors[edit.name] = edit.colorEditor.getHSV();
					currentUpdates.push_back(edit.name);

					if (MouseL.down() && !(edit.colorEditor.getScope().mouseOver() || edit.colorEditor.getTabScope().mouseOver()))
					{
						edittingColor = none;
					}
				}

				//クリック操作
				if (!grabbingColor && !grabbingGroup && !edittingColor)
				{
					for (size_t groupIndex = 0; groupIndex < colorGroups.size(); ++groupIndex)
					{
						for (size_t colorIndex = 0; colorIndex < colorGroups[groupIndex].size(); ++colorIndex)
						{
							const RectF innerScope = getInnerColorScope(WindowIndex(groupIndex, colorIndex));

							if (innerScope.leftClicked())
							{
								const String& name = colorGroups[groupIndex][colorIndex];
								edittingColor = EditColorInfo(name, colors[name]);
								edittingColor.value().colorEditor.colorBoxTL = getColorScope({ groupIndex, colorIndex }).tr();
							}
						}
					}

					if (!edittingColor)
					{
						for (size_t groupIndex = 0; groupIndex < colorGroups.size(); ++groupIndex)
						{
							for (size_t colorIndex = 0; colorIndex < colorGroups[groupIndex].size(); ++colorIndex)
							{
								const RectF scope = getColorScope({ groupIndex, colorIndex });
								if (scope.leftClicked())
								{
									const Vec2 offset = Cursor::PosF() - scope.pos;
									grabbingColor = GrabInfo(colorGroups[groupIndex][colorIndex], offset);
								}
							}
						}
					}

					if (!edittingColor && !grabbingColor)
					{
						for (size_t groupIndex = 0; groupIndex < colorGroups.size(); ++groupIndex)
						{
							if (getGroupOuterScope(groupIndex).leftClicked() && !getGroupInnerScope(groupIndex).mouseOver())
							{
								grabbingGroup = groupIndex;
							}
						}
					}
				}

				Optional<WindowIndex> grabbingColorIndex;
				if (grabbingColor)
				{
					grabbingColorIndex = searchByName(grabbingColor.value().name);
				}

				//描画
				for (size_t groupIndex = 0; groupIndex < colorGroups.size(); ++groupIndex)
				{
					const auto& currentGroup = colorGroups[groupIndex];
					const Vec2& groupTLPos = groupPositions[groupIndex];
					getGroupOuterScope(groupIndex).draw(Palette::Gray);

					for (size_t colorIndex = 0; colorIndex < currentGroup.size(); ++colorIndex)
					{
						unsigned alpha = 255;
						if (grabbingColorIndex && grabbingColorIndex.value() == WindowIndex(groupIndex, colorIndex))
						{
							alpha = 128;
						}
						const Vec2 pos = groupTLPos + Vec2(0, colorIndex)*unitHeight;
						drawColorScope({ groupIndex, colorIndex }, pos, alpha);
					}
				}

				if (grabbingColor)
				{
					getColorScope(searchByName(grabbingColor.value().name).value()).draw(Color(255, 255, 255, 64));
				}
				else if (edittingColor)
				{
					edittingColor.value().colorEditor.draw();
				}

				if (grabbingColorIndex)
				{
					const Vec2 drawPos = Cursor::PosF() - grabbingColor.value().posOffset;
					drawColorScope(grabbingColorIndex.value(), drawPos);
				}
			}

			bool exists(const String& name)const
			{
				return colors.find(name) != colors.end();
			}

			std::unordered_map<String, Color> getUpdates()const
			{
				std::unordered_map<String, Color> result;
				for (const auto& name : currentUpdates)
				{
					result[name] = colors.find(name)->second;
				}
				return result;
			}

			const std::unordered_map<String, ColorF>& getColors()const
			{
				return colors;
			}

			template <class Archive>
			void SIV3D_SERIALIZE(Archive& archive)
			{
				archive(colors, colorGroups, groupPositions);
			}

		private:
			RectF getGroupOuterScope(size_t groupIndex)const
			{
				const Vec2 colorScopeTL = groupPositions[groupIndex];
				const double colorScopesHeight = colorGroups[groupIndex].size()*unitHeight;
				return RectF(colorScopeTL - Vec2(0, tabHeight) - Vec2(groupMargin, groupMargin), width + groupMargin * 2, tabHeight + colorScopesHeight + groupMargin * 2);
			}

			RectF getGroupInnerScope(size_t groupIndex)const
			{
				const Vec2 colorScopeTL = groupPositions[groupIndex];
				const double colorScopesHeight = colorGroups[groupIndex].size()*unitHeight;
				return RectF(colorScopeTL, width, colorScopesHeight);
			}

			RectF getColorScope(const WindowIndex& index)const
			{
				const Vec2 pos = groupPositions[index.groupIndex] + Vec2(0, index.colorIndex)*unitHeight;
				return RectF(pos, width, unitHeight);
			}

			RectF getInnerColorScope(const WindowIndex& index)const
			{
				const Vec2 pos = groupPositions[index.groupIndex] + Vec2(0, index.colorIndex)*unitHeight;
				const RectF scope(pos, width, unitHeight);

				const double innerMergin = 5.0;
				const double innerHeight = unitHeight - innerMergin * 2;
				const double innerWidth = innerHeight * 2;
				const Vec2 innerRectTL = scope.br() - Vec2(innerWidth, innerHeight) - Vec2(innerMergin, innerMergin);

				return RectF(innerRectTL, innerWidth, innerHeight);
			}

			RectF getInnerColorScope(const Vec2& scopeTLPos)const
			{
				const RectF scope(scopeTLPos, width, unitHeight);

				const double innerMergin = 5.0;
				const double innerHeight = unitHeight - innerMergin * 2;
				const double innerWidth = innerHeight * 2;
				const Vec2 innerRectTL = scope.br() - Vec2(innerWidth, innerHeight) - Vec2(innerMergin, innerMergin);

				return RectF(innerRectTL, innerWidth, innerHeight);
			}

			Optional<WindowIndex> searchByName(const String& name)const
			{
				for (size_t groupIndex = 0; groupIndex < colorGroups.size(); ++groupIndex)
				{
					for (size_t colorIndex = 0; colorIndex < colorGroups[groupIndex].size(); ++colorIndex)
					{
						if (name == colorGroups[groupIndex][colorIndex])
						{
							return WindowIndex(groupIndex, colorIndex);
						}
					}
				}

				return none;
			}

			void drawColorScope(const WindowIndex& index, const Vec2& pos, unsigned alpha = 255)const
			{
				const String& name = colorGroups[index.groupIndex][index.colorIndex];
				RectF(pos, width, unitHeight).draw(Color(32, 32, 32, alpha));
				RectF(pos, width, unitHeight).drawFrame(1.0, Color(128, 128, 128, alpha));
				font(name).draw(pos, Color(255, 255, 255, alpha));

				const RectF scope = getColorScope(index);
				if (scope.mouseOver() && !grabbingColor && !grabbingGroup)
				{
					scope.draw(Color(255, 255, 255, 64));
				}

				const RectF innerScope = getInnerColorScope(pos);
				innerScope.draw(Color(colors.find(name)->second).setA(alpha));
				innerScope.drawFrame(1.0, Color(Palette::Gray).setA(alpha));
			}

			Font font = Font(20);

			int unitHeight = 50;
			int width = 400;

			int tabHeight = 50;
			int groupMargin = 1;

			std::unordered_map<String, ColorF> colors;
			std::vector<std::vector<String>> colorGroups;
			std::vector<Vec2> groupPositions;

			std::vector<String> currentUpdates;

			struct GrabInfo
			{
				String name;
				Vec2 posOffset;
				GrabInfo() = default;
				GrabInfo(const String& name, const Vec2& posOffset) :
					name(name),
					posOffset(posOffset)
				{}
			};

			struct EditColorInfo
			{
				String name;
				ColorEditor colorEditor;
				EditColorInfo() = default;
				EditColorInfo(const String& name, const Color& color) :
					name(name),
					colorEditor(color)
				{}
			};

			Optional<GrabInfo> grabbingColor;
			Optional<size_t> grabbingGroup;
			Optional<EditColorInfo> edittingColor;
		};

		struct ServerState
		{
			template <class Archive>
			void SIV3D_SERIALIZE(Archive& archive)
			{
				archive(receivedBuffer, editor);
			}

			ParameterData receivedBuffer;
			MultiColorEditors editor;
		};

		class ParameterEditor
		{
		public:
			static void Update()
			{
				auto& i = instance();
				i.reportUpdate = true;
			}

			static const Color& GetColor(const String& name)
			{
				auto& i = instance();
				if (i.colors.find(name) == i.colors.end())
				{
					std::lock_guard<std::mutex> lock(i.mtx);
					i.colors[name] = RandomColor();
					i.data1.colors[name] = i.colors[name];
				}

				return i.colors[name];
			}

		private:
			ParameterEditor()
			{
				const String directoryName = U"ParameterEditor";
				if (!FileSystem::Exists(directoryName))
				{
					FileSystem::CreateDirectories(directoryName);
				}

				{
					const String versionFileName = directoryName + U"/version.dat";
					const String saveFileName = directoryName + U"/save.dat";
					if (FileSystem::Exists(versionFileName))
					{
						Deserializer<BinaryReader> versionDeserializer(versionFileName);

						unsigned version;
						versionDeserializer(version);

						if (version == EditorVersion)
						{
							phase = Ready;

							//データの復元はサーバー非依存に行える必要があるので初期化時にクライアントでも開く
							if (FileSystem::Exists(saveFileName) && !FileSystem::IsEmpty(saveFileName))
							{
								Deserializer<BinaryReader> deserializer(saveFileName);

								ServerState initialState;
								deserializer(initialState);

								//for (const auto& keyVal : initialState.receivedBuffer.colors)
								for (const auto& keyVal : initialState.editor.getColors())
								{
									colors[keyVal.first] = keyVal.second;
								}
							}
							else
							{
								BinaryWriter writer(saveFileName);
							}
						}
					}
					else
					{
						Serializer<BinaryWriter> serializer(versionFileName);
						serializer(EditorVersion);
						phase = Ready;

						if (FileSystem::Exists(saveFileName))
						{
							phase = Beginning;
						}
						else
						{
							BinaryWriter writer(saveFileName);
						}
					}
				}

				//ここ以降での phase == Beginning はエラー状態として扱う

				{
					const String fileName = directoryName + U"/send.dat";
					Serializer<BinaryWriter> serializer(fileName);
					//phase が Ready になってない時は version.dat と EditorVersion が一致しない可能性がある
					//サーバー側でクライアントの EditorVersion を把握するため送っておく
					serializer(EditorVersion);
				}
				{
					const String fileName = directoryName + U"/receive.dat";
					BinaryWriter writer(fileName);
				}

				if (PMT_RELEASE_FLAG)
				{
					phase = Beginning;
				}
				else
				{
					const auto path = FileSystem::FullPath(directoryName);
					std::array<char32_t, 256> filePath{};
					for (auto ic : Indexed(path))
					{
						filePath[ic.first] = ic.second;
					}
					directoryPath = path;
					directoryWatcher = DirectoryWatcher(directoryPath);

					Serializer<MemoryWriter> serializer;
					serializer(filePath);
					const auto& writer = serializer.getWriter();
					sendData = ByteArray(writer.data(), static_cast<size_t>(writer.size()));

					worker1 = std::thread(ReportNewColors);
				}
			}

			//新しく追加された色をサーバーに送る
			static void ReportNewColors()
			{
				auto& i = instance();
				i.client.connect(IPv4::localhost(), PortNumber);

				while (!i.terminationRequest)
				{
					if (!i.reportUpdate)
					{
						continue;
					}
					i.reportUpdate = false;

					switch (i.phase)
					{
					case ParameterEditor::Beginning:
					{
						Window::SetTitle(U"初期化に失敗");
						break;
					}
					case ParameterEditor::Ready:
					{
						if (i.client.isConnected())
						{
							//Window::SetTitle(U"TCPClient: 接続完了！");

							i.client.send(i.sendData.data(), 4 * 256);
							i.sendData = ByteArray();
							i.phase = WaitingServer;

							break;
						}

						if (i.client.hasError())
						{
							i.client.disconnect();
							//Window::SetTitle(U"TCPClient: 再接続待機中...");
							i.client.connect(IPv4::localhost(), PortNumber);
						}

						break;
					}
					case ParameterEditor::WaitingServer:
					{
						const FilePath receiveFilePath = i.directoryPath + U"receive.dat";
						const FilePath sendFilePath = i.directoryPath + U"send.dat";

						/*
						メインスレッドの方がこっちのスレッドより多く回る可能性がある
						->DirectoryWatcherが捕捉を漏らす可能性がある？あるとしたらここでは使うべきでない

						クライアント側では、send.dat にバージョン情報を書き込んだ後に通信を始める
						サーバー側では、receive.dat にバージョン情報を書き込んだ後に send.dat の中身をクリアする
						したがって、ここで send.dat の中身が空だった時は、確実に receive.dat には有効な値が入っているはず
						*/

						if (FileSystem::IsEmpty(sendFilePath))
						{
							{
								Deserializer<BinaryReader> deserializer(receiveFilePath);

								unsigned version;
								deserializer(version);

								if (version != EditorVersion)
								{
									i.phase = Beginning;
								}
								else
								{
									i.phase = Running;
									i.client.disconnect();
								}

							}

							BinaryWriter writer(receiveFilePath);
						}

						break;
					}
					case ParameterEditor::Running:
					{
						std::lock_guard<std::mutex> lock(i.mtx);

						const FilePath receiveFilePath = i.directoryPath + U"receive.dat";
						for (const auto& pathAction : i.directoryWatcher.retrieveChanges())
						{
							if (pathAction.first == receiveFilePath && !FileSystem::IsEmpty(receiveFilePath))
							{
								//ここのデシリアライズで失敗した(理由不明)
								try
								{
									Deserializer<BinaryReader> deserializer(receiveFilePath);

									ParameterData receivedData;
									deserializer(receivedData);

									for (const auto& keyVal : receivedData.colors)
									{
										i.colors[keyVal.first] = keyVal.second;
									}
								}
								catch (std::exception& e)
								{
									Logger << Unicode::Widen(e.what());
								}

								BinaryWriter writer(receiveFilePath);
							}
						}

						const FilePath sendFilePath = i.directoryPath + U"send.dat";
						if (!i.data1.colors.empty() && FileSystem::IsEmpty(sendFilePath))
						{
							try
							{
								Serializer<BinaryWriter> serializer(sendFilePath);
								serializer(i.data1);
								i.data1 = ParameterData();
							}
							catch (std::exception& e)
							{
								Logger << Unicode::Widen(e.what());
							}
						}

						break;
					}
					default: break;
					}
				}
			}

			ParameterEditor(const ParameterEditor&) = delete;

			~ParameterEditor()
			{
				terminateAllThreads();
			}

			static ParameterEditor& instance()
			{
				static ParameterEditor obj;
				return obj;
			}

			void terminateAllThreads()
			{
				terminationRequest = true;
				if (worker1.joinable())
				{
					worker1.join();
				}
			}

			enum Phase { Beginning, Ready, WaitingServer, Running };
			//Beginning     初期状態(or初期化に失敗)
			//Ready         クライアントとセーブデータのバージョン番号の一致を確認(通信待機状態)
			//WaitingServer ディレクトリ情報の送信完了(receive.datの更新待機状態)
			//Running       クライアントとサーバーのバージョン番号の一致を確認(通常状態)

			TCPClient client;
			uint32 receivedVal = 0;
			std::unordered_map<String, Color> colors;
			ParameterData data1;

			ByteArray sendData;
			String directoryPath;
			DirectoryWatcher directoryWatcher;

			std::thread worker1;
			std::mutex mtx;

			bool terminationRequest = false;
			bool reportUpdate = false;

			Phase phase = Beginning;
		};
	}

	inline void Update()
	{
		detailImpl::ParameterEditor::Update();
	}

	inline const Color& GetColor(const String& name)
	{
		return detailImpl::ParameterEditor::GetColor(name);
	}
}
