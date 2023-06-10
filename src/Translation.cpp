#include "Translation.h"

namespace Translation
{
	std::string Manager::GetLanguage()
	{
		const auto iniSettingCollection = RE::INISettingCollection::GetSingleton();
        const auto setting = iniSettingCollection ? iniSettingCollection->GetSetting("sLanguage:General") : nullptr;

		return (setting && setting->GetType() == RE::Setting::Type::kString) ? setting->data.s : "ENGLISH"s;
	}

	void Manager::BuildTranslationMap()
	{
		std::filesystem::path path{ fmt::format(R"(Data\Interface\Translations\PhotoMode_{}.txt)", GetLanguage()) };

		if (!LoadTranslation(path)) {
			LoadTranslation(R"(Data\Interface\Translations\PhotoMode_ENGLISH.txt)"sv);
		}
	}

	bool Manager::LoadTranslation(const std::filesystem::path& a_path)
	{
		if (!std::filesystem::exists(a_path)) {
			return false;
		}

		std::wfstream filestream(a_path);
		if (!filestream.good()) {
			return false;
		} else {
			logger::info("Reading translations from {}...", a_path.string());
		}

		filestream.imbue(std::locale(filestream.getloc(), new std::codecvt_utf16<wchar_t, 0x10FFFF, std::little_endian>));

		// check if the BOM is UTF-16
		constexpr wchar_t BOM_UTF16LE = 0xFEFF;
		if (filestream.get() != BOM_UTF16LE) {
			logger::info("BOM Error, file must be encoded in UCS-2 LE.");
			return false;
		}

		std::wstring line, key, value;

		while (std::getline(filestream, line)) {
			std::wstringstream ss(line);
			ss >> key;
			// remove leading whitespace
			std::getline(ss >> std::ws, value);
			// remove space/new line at end
			if (std::isspace(value.back())) {
				value.pop_back();
			}
			translationMap.emplace(*stl::utf16_to_utf8(key), *stl::utf16_to_utf8(value));
		}

		return true;
	}
}
