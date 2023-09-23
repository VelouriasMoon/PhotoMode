#include "Character.h"
#include "Utilities/Utils.h"

namespace PhotoMode
{
	namespace MFG
	{
		void Data::Expression::ApplyExpression(RE::Actor* a_actor) const
		{
			if (const auto faceData = a_actor->GetFaceGenAnimationData()) {
				if (modifier == 0) {
					faceData->ClearExpressionOverride();
					faceData->Reset(0.0f, true, false, false, false);
					if (a_actor->IsPlayerRef()) {
						RE::BSFaceGenManager::GetSingleton()->isReset = true;
					}
				} else {
					faceData->exprOverride = false;
					faceData->SetExpressionOverride(modifier - 1, static_cast<float>(strength / 100.0f));
					faceData->exprOverride = true;
				}
			}
		}

		void Data::Modifier::ApplyModifier(std::uint32_t idx, RE::Actor* a_actor) const
		{
			if (const auto faceData = a_actor->GetFaceGenAnimationData()) {
				RE::BSSpinLockGuard locker(faceData->lock);
				faceData->modifierKeyFrame.SetValue(idx, static_cast<float>(strength / 100.0f));
			}
		}

		void Data::Modifier::ApplyPhenome(std::uint32_t idx, RE::Actor* a_actor) const
		{
			if (const auto faceData = a_actor->GetFaceGenAnimationData()) {
				RE::BSSpinLockGuard locker(faceData->lock);
				faceData->phenomeKeyFrame.SetValue(idx, static_cast<float>(strength / 100.0f));
			}
		}

		void Data::Revert(RE::Actor* a_actor)
		{
			if (const auto faceData = a_actor->GetFaceGenAnimationData()) {
				faceData->ClearExpressionOverride();
				faceData->Reset(0.0f, true, true, true, false);
				if (a_actor->IsPlayerRef()) {
					RE::BSFaceGenManager::GetSingleton()->isReset = true;
				}
			}

			expressionData.modifier = 0;
			expressionData.strength = 0;

			for (std::uint32_t i = 0; i < phonemes.size(); i++) {
				phonemeData[i].strength = 0;
			}
			for (std::uint32_t i = 0; i < modifiers.size(); i++) {
				modifierData[i].strength = 0;
			}
		}
	}

	void Character::State::Get(const RE::Actor* a_actor)
	{
		visible = a_actor->Get3D() ? !a_actor->Get3D()->GetAppCulled() : false;

		rotZ = a_actor->GetAngleZ();
		pos = a_actor->GetPosition();
	}

	void Character::RevertIdle() const
	{
		if (const auto currentProcess = character ? character->currentProcess : nullptr) {
			currentProcess->StopCurrentIdle(character, true);
			currentProcess->PlayIdle(character, resetRootIdle, nullptr);
		}
	}

	Character::Character(RE::Actor* a_actor) :
		character(a_actor)
	{
		if (a_actor->IsPlayerRef()) {
			characterName = TRANSLATE_S("$PM_Player");
		} else if (const auto actorbase = a_actor->GetActorBase(); actorbase && actorbase->IsUnique()) {
			characterName = a_actor->GetName();
		} else {
			characterName = fmt::format("{} [0x{:X}]", a_actor->GetName(), a_actor->GetFormID());
		}

		GetOriginalState();

		if (!character->IsPlayerRef()) {
			character->InitiateDoNothingPackage();
		}
	}

	void Character::GetOriginalState()
	{
		originalState.Get(character);

		effectShaders.InitForms();
		effectVFX.InitForms();
		idles.InitForms();
		weapons.InitForms(character->GetInventory(), RE::FormType::Weapon);
		armors.InitForms(character->GetInventory(), RE::FormType::Armor);
		spellsR.InitMagic(character->addedSpells);
		spellsL.InitMagic(character->addedSpells);
	}

	void Character::RevertState()
	{
		// revert current values
		currentState.pos = RE::NiPoint3();

		if (rotationChanged) {
			character->SetRotationZ(originalState.rotZ);
		}
		if (positionChanged) {
			character->SetPosition(originalState.pos, true);
		}
		if (positionChanged || rotationChanged) {
			character->UpdateActor3DPosition();

			positionChanged = false;
			rotationChanged = false;
		}

		if (!currentState.visible) {
			if (const auto root = character->Get3D()) {
				root->CullGeometry(false);
			}
			currentState.visible = true;
		}

		// reset expressions
		mfgData.Revert(character);

		// revert idles
		idles.Reset();
		if (idlePlayed) {
			RevertIdle();
			idlePlayed = false;
		}

		//revert inventroy
		weapons.ResetIndex();
		weapons.SetValid(false);
		armors.ResetIndex();
		armors.SetValid(false);
		spellsR.ResetIndex();
		spellsR.SetValid(false);
		spellsL.ResetIndex();
		spellsL.SetValid(false);

		// revert effects
		effectShaders.Reset();
		effectVFX.Reset();

		if (vfxPlayed || effectsPlayed) {
			if (const auto processLists = RE::ProcessLists::GetSingleton()) {
				const auto handle = character->CreateRefHandle();
				processLists->ForEachMagicTempEffect([&](RE::BSTempEffect& a_effect) {
					if (const auto referenceEffect = a_effect.As<RE::ReferenceEffect>()) {
						if (referenceEffect->target == handle) {
							referenceEffect->finished = true;
						}
					}
					return RE::BSContainer::ForEachResult::kContinue;
				});
			}
			vfxPlayed = false;
			effectsPlayed = false;
		}

		if (!character->IsPlayerRef()) {
			character->EndInterruptPackage(false);
		}
	}

	const char* Character::GetName() const
	{
		return characterName.c_str();
	}

	void Character::Draw(bool a_resetTabs)
	{
		if (ImGui::CheckBox(character->IsPlayerRef() ? "$PM_ShowPlayer"_T : "$PM_ShowCharacter"_T, &currentState.visible)) {
			if (const auto root = character->Get3D()) {
				root->CullGeometry(!currentState.visible);
			}
		}

		ImGui::Spacing();

		ImGui::BeginDisabled(!currentState.visible);
		{
			if (ImGui::BeginTabBarCustom("Player#TopBar", 0)) {
				// ugly af, improve later
				const float width = ImGui::GetContentRegionAvail().x / 5;

				if (a_resetTabs) {
					ImGui::SetKeyboardFocusHere();
				}

				if (character->GetFaceGenAnimationData()) {
					ImGui::SetNextItemWidth(width);
					if (ImGui::OpenTabOnHover("$PM_Expressions"_T, a_resetTabs ? ImGuiTabItemFlags_SetSelected : 0)) {
						using namespace MFG;

						if (ImGui::EnumSlider("$PM_Expression"_T, &mfgData.expressionData.modifier, expressions)) {
							mfgData.expressionData.strength = 0;
						}
						ImGui::Indent();
						{
							ImGui::BeginDisabled(mfgData.expressionData.modifier == 0);
							{
								if (ImGui::Slider("$PM_Intensity"_T, &mfgData.expressionData.strength, 0, 100)) {
									mfgData.expressionData.ApplyExpression(character);
								}
							}
							ImGui::EndDisabled();
						}
						ImGui::Unindent();

						ImGui::Spacing();

						if (ImGui::TreeNode("$PM_Phoneme"_T)) {
							for (std::uint32_t i = 0; i < phonemes.size(); i++) {
								if (ImGui::Slider(TRANSLATE(phonemes[i]), &mfgData.phonemeData[i].strength, 0, 100)) {
									mfgData.phonemeData[i].ApplyPhenome(i, character);
								}
							}
							ImGui::TreePop();
						}

						if (ImGui::TreeNode("$PM_Modifier"_T)) {
							for (std::uint32_t i = 0; i < modifiers.size(); i++) {
								if (ImGui::Slider(TRANSLATE(modifiers[i]), &mfgData.modifierData[i].strength, 0, 100)) {
									mfgData.modifierData[i].ApplyModifier(i, character);
								}
							}
							ImGui::TreePop();
						}
						ImGui::EndTabItem();
					}
				}

				ImGui::SetNextItemWidth(width);
				if (ImGui::OpenTabOnHover("$PM_Poses"_T)) {
					idles.GetFormResultFromCombo([&](const auto& a_idle) {
						if (idlePlayed) {
							RevertIdle();
						}
						if (const auto currentProcess = character->currentProcess) {
							if (currentProcess->PlayIdle(character, a_idle, nullptr)) {
								idlePlayed = true;
							}
						}
					},
						character);
					ImGui::EndTabItem();
				}

				ImGui::SetNextItemWidth(width);
				if (ImGui::OpenTabOnHover("$PM_Inventory"_T)) {
					weapons.GetFormResultFromCombo([&](const auto& a_item) {
						
						if (character->GetEquippedObject(false) == a_item || character->GetEquippedObject(true) == a_item)
							RE::ActorEquipManager::GetSingleton()->UnequipObject(character, a_item);
						else
							RE::ActorEquipManager::GetSingleton()->EquipObject(character, a_item);
					},
						character);
					armors.GetFormResultFromCombo([&](const auto& a_item) {
						auto inv = character->GetInventory();
						RE::TESObjectREFR::InventoryItemMap::const_iterator item = inv.find(a_item);
						if (item != inv.end()) {
							if (item->second.second->IsWorn())
								RE::ActorEquipManager::GetSingleton()->UnequipObject(character, a_item);
							else
								RE::ActorEquipManager::GetSingleton()->EquipObject(character, a_item);
						}
					},
						character);
					spellsR.GetFormResultFromCombo([&](const auto& a_spell) {
						if (character->GetEquippedObject(false) == a_spell)
						{
							RE::ActorEquipManager::GetSingleton()->UnequipObject(character, a_spell);
							character->DeselectSpell(a_spell);
						}
						else
							RE::ActorEquipManager::GetSingleton()->EquipSpell(character, a_spell, Utils::Slot::GetRightHandSlot());
						
					},
						character);
					spellsL.GetFormResultFromCombo([&](const auto& a_spell) {
						if (character->GetEquippedObject(true) == a_spell) {
							RE::ActorEquipManager::GetSingleton()->UnequipObject(character, a_spell);
							character->DeselectSpell(a_spell);
						} else
							RE::ActorEquipManager::GetSingleton()->EquipSpell(character, a_spell, Utils::Slot::GetLeftHandSlot());
					},
						character);
					character->Update(1.0f);
					ImGui::EndTabItem();
				}

				ImGui::SetNextItemWidth(width);
				if (ImGui::OpenTabOnHover("$PM_Effects"_T)) {
					effectShaders.GetFormResultFromCombo([&](const auto& a_effectShader) {
						character->ApplyEffectShader(a_effectShader);
						effectsPlayed = true;
					});
					effectVFX.GetFormResultFromCombo([&](const auto& a_vfx) {
						if (const auto effectShader = a_vfx->data.effectShader) {
							character->ApplyEffectShader(effectShader, -1, nullptr, a_vfx->data.flags.any(RE::BGSReferenceEffect::Flag::kFaceTarget), a_vfx->data.flags.any(RE::BGSReferenceEffect::Flag::kAttachToCamera));
						}
						if (const auto artObject = a_vfx->data.artObject) {
							character->ApplyArtObject(artObject, -1, nullptr, a_vfx->data.flags.any(RE::BGSReferenceEffect::Flag::kFaceTarget), a_vfx->data.flags.any(RE::BGSReferenceEffect::Flag::kAttachToCamera));
						}
						vfxPlayed = true;
					});
					ImGui::EndTabItem();
				}

				ImGui::SetNextItemWidth(width);
				if (ImGui::OpenTabOnHover("$PM_Transforms"_T)) {
					currentState.rotZ = RE::rad_to_deg(character->GetAngleZ());
					if (ImGui::Slider("$PM_Rotation"_T, &currentState.rotZ, 0.0f, 360.0f)) {
						character->SetRotationZ(RE::deg_to_rad(currentState.rotZ));
						rotationChanged = true;
					}

					bool update = ImGui::Slider("$PM_PositionLeftRight"_T, &currentState.pos.x, -100.0f, 100.0f);
					update |= ImGui::Slider("$PM_PositionNearFar"_T, &currentState.pos.y, -100.0f, 100.0f);
					// update |= ImGui::Slider("Elevation", &currentState.pos.z, -100.0f, 100.0f);

					if (update) {
						character->SetPosition({ originalState.pos + currentState.pos }, true);
						positionChanged = true;
					}

					ImGui::EndTabItem();
				}
				ImGui::EndTabBar();
			}
		}
		ImGui::EndDisabled();
	}
}
