#include "Manager.h"

#include "Hotkeys.h"
#include "ImGui/IconsFonts.h"
#include "ImGui/Widgets.h"
#include "Screenshots/Manager.h"

namespace PhotoMode
{
	void Manager::Register()
	{
		RE::UI::GetSingleton()->AddEventSink(GetSingleton());

		logger::info("Registered for menu open/close event");
	}

	void Manager::LoadMCMSettings(const CSimpleIniA& a_ini)
	{
		freeCameraSpeed = a_ini.GetDoubleValue("Settings", "fFreeCameraTranslationSpeed", freeCameraSpeed);
		freezeTimeOnStart = a_ini.GetBoolValue("Settings", "bFreezeTimeOnStart", freezeTimeOnStart);
	}

	bool Manager::IsValid()
	{
		static constexpr std::array badMenus{
			RE::MainMenu::MENU_NAME,
			RE::MistMenu::MENU_NAME,
			RE::JournalMenu::MENU_NAME,
			RE::InventoryMenu::MENU_NAME,
			RE::MagicMenu::MENU_NAME,
			RE::MapMenu::MENU_NAME,
			RE::BookMenu::MENU_NAME,
			RE::LockpickingMenu::MENU_NAME,
			RE::StatsMenu::MENU_NAME,
			RE::ContainerMenu::MENU_NAME,
			RE::DialogueMenu::MENU_NAME,
			RE::CraftingMenu::MENU_NAME,
			RE::TweenMenu::MENU_NAME,
			RE::SleepWaitMenu::MENU_NAME,
			RE::RaceSexMenu::MENU_NAME,
			"CustomMenu"sv
		};

		if (const auto player = RE::PlayerCharacter::GetSingleton(); !player || !player->Get3D() && !player->Get3D(true)) {
			return false;
		}

		if (const auto UI = RE::UI::GetSingleton(); !UI || std::ranges::any_of(badMenus, [&](const auto& menuName) { return UI->IsMenuOpen(menuName); })) {
			return false;
		}

		return true;
	}

	bool Manager::ShouldBlockInput()
	{
		return RE::UI::GetSingleton()->IsMenuOpen(RE::Console::MENU_NAME);
	}

	bool Manager::IsActive() const
	{
		return activated;
	}

	void Manager::Activate()
	{
		cameraTab.GetOriginalState();
		timeTab.GetOriginalState();
		playerTab.GetOriginalState();
		filterTab.GetOriginalState();

		const auto pcCamera = RE::PlayerCamera::GetSingleton();
		if (pcCamera->IsInFirstPerson()) {
			cameraState = RE::CameraState::kFirstPerson;
		} else if (pcCamera->IsInFreeCameraMode()) {
			cameraState = RE::CameraState::kFree;
		} else {
			cameraState = RE::CameraState::kThirdPerson;
		}

		menusAlreadyHidden = !RE::UI::GetSingleton()->IsShowingMenus();

		// disable saving
		RE::PlayerCharacter::GetSingleton()->byCharGenFlag.set(RE::PlayerCharacter::ByCharGenFlag::kDisableSaving);

		// toggle freecam
		if (cameraState != RE::CameraState::kFree) {
			RE::PlayerCamera::GetSingleton()->ToggleFreeCameraMode(false);
		}

		// apply mcm settings
		FreeCamera::translateSpeed = freeCameraSpeed;
		if (freezeTimeOnStart) {
			RE::Main::GetSingleton()->freezeTime = true;
		}

		activated = true;
	}

	void Manager::Revert(bool a_deactivate)
	{
		const std::int32_t tabIndex = a_deactivate ? -1 : currentTab;

		// Camera
		if (tabIndex == -1 || tabIndex == kCamera) {
			cameraTab.RevertState(a_deactivate);
			if (!a_deactivate) {
				FreeCamera::translateSpeed = freeCameraSpeed;
			}
			revertENB = true;
		}
		// Time/Weather
		if (tabIndex == -1 || tabIndex == kTime) {
			timeTab.RevertState();
		}
		// Player
		if (tabIndex == -1 || tabIndex == kPlayer) {
			playerTab.RevertState();
		}
		// Filters
		if (tabIndex == -1 || tabIndex == kFilters) {
			filterTab.RevertState(tabIndex == -1);
		}

		if (a_deactivate) {
			// reset UI
			if (!menusAlreadyHidden && !RE::UI::GetSingleton()->IsShowingMenus()) {
				RE::UI::GetSingleton()->ShowMenus(true);
			}
			resetWindow = true;
			resetPlayerTabs = true;
		} else {
			RE::PlaySound("UIMenuOK");

			const auto notification = fmt::format("{}", resetAll ? "$PM_ResetNotifAll"_T : TRANSLATE(tabResetNotifs[currentTab]));
			RE::DebugNotification(notification.c_str());

			if (resetAll) {
				resetAll = false;
			}
		}
	}

	void Manager::Deactivate()
	{
		Revert(true);

		// reset camera
		switch (cameraState) {
		case RE::CameraState::kFirstPerson:
			RE::PlayerCamera::GetSingleton()->ForceFirstPerson();
			break;
		case RE::CameraState::kThirdPerson:
			RE::PlayerCamera::GetSingleton()->ForceThirdPerson();
			break;
		default:
			break;
		}

		// reset controls
		allowTextInput = false;
		RE::ControlMap::GetSingleton()->AllowTextInput(false);
		RE::ControlMap::GetSingleton()->ToggleControls(controlFlags, true);

		// allow saving
		RE::PlayerCharacter::GetSingleton()->byCharGenFlag.reset(RE::PlayerCharacter::ByCharGenFlag::kDisableSaving);

		activated = false;
	}

	void Manager::ToggleActive()
	{
		if (!IsActive()) {
			if (IsValid() && !ShouldBlockInput()) {
				RE::PlaySound("UIMenuOK");
				Activate();
			}
		} else {
			if (!ImGui::GetIO().WantTextInput) {
				Deactivate();
				RE::PlaySound("UIMenuCancel");
			}
		}
	}

	bool Manager::GetResetAll() const
	{
		return resetAll;
	}

	void Manager::DoResetAll()
	{
		resetAll = true;
	}

	void Manager::NavigateTab(bool a_left)
	{
		if (a_left) {
			currentTab = (currentTab - 1 + tabs.size()) % tabs.size();
		} else {
			currentTab = (currentTab + 1) % tabs.size();
		}
		updateKeyboardFocus = true;
	}

	float Manager::GetViewRoll(const float a_fallback) const
	{
		return IsActive() ? cameraTab.GetViewRoll() : a_fallback;
	}

	bool Manager::OnFrameUpdate()
	{
		if (!IsValid()) {
			Deactivate();
			return false;
		}

		// disable controls
		if (ImGui::GetIO().WantTextInput) {
			if (!allowTextInput) {
				allowTextInput = true;
				RE::ControlMap::GetSingleton()->AllowTextInput(true);
			}
		} else if (allowTextInput) {
			allowTextInput = false;
			RE::ControlMap::GetSingleton()->AllowTextInput(false);
		}
		RE::ControlMap::GetSingleton()->ToggleControls(controlFlags, false);

		timeTab.OnFrameUpdate();

		return true;
	}

	void Manager::UpdateENBParams()
	{
		if (IsActive()) {
			cameraTab.UpdateENBParams();
		}
	}

	void Manager::RevertENBParams()
	{
		if (revertENB) {
			cameraTab.RevertENBParams();
			revertENB = false;
		}
	}

	void Manager::Draw()
	{
		const ImGuiViewport* viewport = ImGui::GetMainViewport();

		ImGui::SetNextWindowPos(viewport->Pos);
		ImGui::SetNextWindowSize(viewport->Size);

		ImGui::Begin("##Main", nullptr, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBringToFrontOnFocus);
		{
			// render hierachy
			CameraGrid::Draw();
			DrawBar();
			DrawControls();
		}
		ImGui::End();
	}

	void Manager::DrawControls()
	{
		const auto viewport = ImGui::GetMainViewport();
		const auto io = ImGui::GetIO();
		const auto styling = ImGui::GetStyle();

		const static auto center = viewport->GetCenter();

		const static auto third_width = viewport->Size.x / 3;
		const static auto third_height = viewport->Size.y / 3;

		ImGui::SetNextWindowPos(ImVec2(center.x + third_width, center.y + third_height * 0.8f), ImGuiCond_Always, ImVec2(0.5, 0.5));
		ImGui::SetNextWindowSize(ImVec2(viewport->Size.x / 3.25f, viewport->Size.y / 3.125f));

		constexpr auto windowFlags = ImGuiWindowFlags_NoMouseInputs | ImGuiWindowFlags_NoDecoration;

		ImGui::Begin("$PM_Title_Menu"_T, nullptr, windowFlags);
		{
			ImGui::ExtendWindowPastBorder();

			if (resetWindow) {
				currentTab = kCamera;
			}

			// Q [Tab Tab Tab Tab Tab] E
			ImGui::BeginGroup();
			{
				const auto buttonSize = ImGui::ButtonIcon(MANAGER(Hotkeys)->PreviousTabKey());
				ImGui::SameLine();

				const float tabWidth = (ImGui::GetContentRegionAvail().x - (buttonSize.x + styling.ItemSpacing.x * tabs.size())) / tabs.size();

				ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
				for (std::int32_t i = 0; i < tabs.size(); ++i) {
					if (currentTab != i) {
						ImGui::BeginDisabled(true);
					} else {
						ImGui::PushFont(MANAGER(IconFont)->GetLargeFont());
					}
					ImGui::Button(tabIcons[i], ImVec2(tabWidth, ImGui::GetFrameHeightWithSpacing()));
					if (currentTab != i) {
						ImGui::EndDisabled();
					} else {
						ImGui::PopFont();
					}
					ImGui::SameLine();
				}
				ImGui::PopStyleColor();
				ImGui::PopItemFlag();

				ImGui::SameLine();
				ImGui::ButtonIcon(MANAGER(Hotkeys)->NextTabKey());
			}
			ImGui::EndGroup();

			//		CAMERA
			// ----------------
			ImGui::CenteredText(TRANSLATE(tabs[currentTab]));
			ImGui::SeparatorEx(ImGuiSeparatorFlags_Horizontal, 3.0f);

			// content
			ImGui::SetNextWindowBgAlpha(0.0f);  // child bg color is added ontop of window
			ImGui::BeginChild("##PhotoModeChild", ImVec2(0, 0), false, windowFlags);
			{
				ImGui::Spacing();

				if (updateKeyboardFocus) {
					if (currentTab == TAB_TYPE::kPlayer) {
						resetPlayerTabs = true;
					}

					ImGui::SetKeyboardFocusHere();
					RE::PlaySound("UIJournalTabsSD");

					updateKeyboardFocus = false;
				}

				switch (currentTab) {
				case TAB_TYPE::kCamera:
					{
						if (resetWindow) {
							ImGui::SetKeyboardFocusHere();
							resetWindow = false;
						}
						cameraTab.Draw();
					}
					break;
				case TAB_TYPE::kTime:
					timeTab.Draw();
					break;
				case TAB_TYPE::kPlayer:
					{
						playerTab.Draw(resetPlayerTabs);

						if (resetPlayerTabs) {
							resetPlayerTabs = false;
						}
					}
					break;
				case TAB_TYPE::kFilters:
					filterTab.Draw();
					break;
				default:
					break;
				}

				if (ImGui::IsKeyReleased(ImGuiKey_Escape) && (!ImGui::IsAnyItemFocused() || !ImGui::IsWindowFocused())) {
					Deactivate();
					RE::PlaySound("UIMenuCancel");
				}
			}
			ImGui::EndChild();
		}
		ImGui::End();
	}

	void Manager::DrawBar() const
	{
		const auto viewport = ImGui::GetMainViewport();

		const static auto center = viewport->GetCenter();
		const static auto offset = viewport->Size.y / 20.25f;

		ImGui::SetNextWindowPos(ImVec2(center.x, viewport->Size.y - offset), ImGuiCond_Always, ImVec2(0.5, 0.5));

		ImGui::Begin("##Bar", nullptr, ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize);  // same offset as control window
		{
			ImGui::ExtendWindowPastBorder();

			const static auto takePhotoLabel = "$PM_TAKEPHOTO"_T;
			const static auto toggleMenusLabel = "$PM_TOGGLEMENUS"_T;
			const auto        resetLabel = GetResetAll() ? "$PM_RESET_ALL"_T : "$PM_RESET"_T;
			const static auto togglePMLabel = "$PM_EXIT"_T;

			const auto& takePhotoIcon = MANAGER(Hotkeys)->TakePhotoIcon();
			const auto& toggleMenusIcon = MANAGER(Hotkeys)->ToggleMenusIcon();
			const auto& resetIcon = MANAGER(Hotkeys)->ResetIcon();
			const auto& togglePMIcons = MANAGER(Hotkeys)->TogglePhotoModeIcons();

			// calc total elements width
			const ImGuiStyle& style = ImGui::GetStyle();

			float width = 0.0f;

			const auto calc_width = [&](const IconFont::ImageData* a_icon, const char* a_textLabel) {
				width += a_icon->size.x;
				width += style.ItemSpacing.x;
				width += ImGui::CalcTextSize(a_textLabel).x;
				width += style.ItemSpacing.x;
			};

			calc_width(takePhotoIcon, takePhotoLabel);
			calc_width(toggleMenusIcon, toggleMenusLabel);
			calc_width(resetIcon, resetLabel);

			for (const auto& icon : togglePMIcons) {
				width += icon->size.x;
			}
			width += style.ItemSpacing.x;
			width += ImGui::CalcTextSize(togglePMLabel).x;

			// align at center
			ImGui::AlignForWidth(width);

			// draw
			constexpr auto draw_button = [](const IconFont::ImageData* a_icon, const char* a_textLabel) {
				ImGui::ButtonIconWithLabel(a_textLabel, a_icon, true);
				ImGui::SameLine();
			};

			draw_button(takePhotoIcon, takePhotoLabel);
			draw_button(toggleMenusIcon, toggleMenusLabel);
			draw_button(resetIcon, resetLabel);

			ImGui::ButtonIconWithLabel(togglePMLabel, togglePMIcons, true);
		}
		ImGui::End();
	}

	EventResult Manager::ProcessEvent(const RE::MenuOpenCloseEvent* a_evn, RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
	{
		if (!a_evn || !a_evn->opening) {
			return EventResult::kContinue;
		}

		const auto UI = RE::UI::GetSingleton();

		if (a_evn->menuName == RE::JournalMenu::MENU_NAME) {
			const auto menu = UI->GetMenu<RE::JournalMenu>(RE::JournalMenu::MENU_NAME);

			if (const auto& view = menu ? menu->systemTab.view : RE::GPtr<RE::GFxMovieView>()) {
				RE::GFxValue page;
				if (!view->GetVariable(&page, "_root.QuestJournalFader.Menu_mc.SystemFader.Page_mc")) {
					return EventResult::kContinue;
				}

				std::array<RE::GFxValue, 1> args;
				args[0] = true;
				if (!page.Invoke("SetShowMod", nullptr, args.data(), args.size())) {
					return EventResult::kContinue;
				}

				RE::GFxValue categoryList;
				if (page.GetMember("CategoryList", &categoryList)) {
					RE::GFxValue entryList;
					if (categoryList.GetMember("entryList", &entryList)) {
						RE::GFxValue entry;
						view->CreateObject(&entry);
						entry.SetMember("text", TRANSLATE("$PM_Title_Menu"));

						entryList.SetElement(3, entry);
						categoryList.Invoke("InvalidateData");
					}
				}
			}
		} else if (a_evn->menuName == RE::ModManagerMenu::MENU_NAME) {
			if (UI->IsMenuOpen(RE::JournalMenu::MENU_NAME)) {
				const auto msgQueue = RE::UIMessageQueue::GetSingleton();

				msgQueue->AddMessage(RE::ModManagerMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
				msgQueue->AddMessage(RE::JournalMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);

				RE::PlaySound("UIMenuOK");
				Activate();
			}
		}

		return EventResult::kContinue;
	}
}
