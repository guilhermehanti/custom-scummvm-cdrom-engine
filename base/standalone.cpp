/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifdef SCUMMVM_STANDALONE

#define FORBIDDEN_SYMBOL_EXCEPTION_printf

#include "base/standalone.h"
#include "base/standalone_config.h"
#include "base/plugins.h"

#include "backends/fs/abstract-fs.h"
#include "backends/fs/fs-factory.h"
#include "backends/keymapper/keymapper.h"

#include "common/config-manager.h"
#include "common/debug.h"
#include "common/events.h"
#include "common/formats/iso9660.h"
#include "common/fs.h"
#include "common/language.h"
#include "common/platform.h"
#include "common/system.h"
#include "common/textconsole.h"
#include "common/translation.h"

#include "engines/engine.h"
#include "engines/metaengine.h"

#include "gui/browser.h"
#include "gui/gui-manager.h"
#include "gui/message.h"
#include "gui/options.h"
#include "gui/widgets/scrollcontainer.h"
#include "gui/widgets/tab.h"

#if defined(WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "backends/platform/sdl/win32/win32.h"
#include "backends/platform/sdl/win32/win32_wrapper.h"
#endif

namespace Standalone {

namespace {

static const GameConfig kGameConfig = {
	STANDALONE_APP_ID,
	STANDALONE_DISPLAY_NAME,
	STANDALONE_ENGINE_ID,
	STANDALONE_GAME_ID,
	STANDALONE_EXPECTED_LANGUAGE,
	STANDALONE_EXPECTED_PLATFORM,
	STANDALONE_EXPECTED_EXTRA,
	STANDALONE_TARGET_NAME,
	STANDALONE_RUNTIME_ICON_FILE
};

static bool g_restartRequested = false;

static Common::U32String utf8Str(const char *s) {
	return Common::U32String(s, Common::kUtf8);
}

static bool hostFileExists(const Common::String &nativePath) {
	if (!g_system || !g_system->getFilesystemFactory())
		return false;
	Common::ScopedPtr<AbstractFSNode> node(g_system->getFilesystemFactory()->makeFileNodePath(nativePath));
	return node && node->exists() && !node->isDirectory();
}

static bool pickIsoFile(Common::Path &chosenPath) {
	const GameConfig &cfg = getConfig();
	Common::U32String title = utf8Str("Selecione a ISO original de ") + utf8Str(cfg.displayName);

#if defined(WIN32)
	HWND owner = nullptr;
	OSystem_Win32 *win32Sys = dynamic_cast<OSystem_Win32 *>(g_system);
	if (win32Sys)
		owner = win32Sys->getWindowHandle();

	Common::Path initialDir;
	if (ConfMan.hasKey("browser_lastpath", Common::ConfigManager::kApplicationDomain)) {
		initialDir = ConfMan.getPath("browser_lastpath", Common::ConfigManager::kApplicationDomain);
	} else if (ConfMan.hasGameDomain(cfg.targetName) && ConfMan.hasKey("path", cfg.targetName)) {
		initialDir = ConfMan.getPath("path", cfg.targetName).getParent();
	}

	if (Win32::showIsoFileDialog(owner, title, initialDir, chosenPath)) {
		Common::Path parent = chosenPath.getParent();
		if (!parent.empty())
			ConfMan.setPath("browser_lastpath", parent, Common::ConfigManager::kApplicationDomain);
		return true;
	}
	return false;
#else
	GUI::BrowserDialog browser(title, false);
	if (browser.runModal() > 0) {
		chosenPath = browser.getResult().getPath();
		return true;
	}
	return false;
#endif
}

class StandaloneOptionsDialog : public GUI::GlobalOptionsDialog {
public:
	StandaloneOptionsDialog()
		: GUI::GlobalOptionsDialog(nullptr),
		  _engineOptions(nullptr) {
	}

	void apply() override {
		if (_engineOptions) {
			_engineOptions->save();
		}

		bool wasOverlayVisible = g_system->isOverlayVisible();

		GUI::GlobalOptionsDialog::apply();

		// Never allow returning to a non-existent launcher in standalone mode.
		ConfMan.setBool("gui_return_to_launcher_at_exit", false, Common::ConfigManager::kApplicationDomain);
		ConfMan.flushToDisk();

		if (wasOverlayVisible && !g_system->isOverlayVisible()) {
			g_system->showOverlay();
			g_gui.checkScreenChange();
		}

		if (g_engine) {
			g_system->applyBackendSettings();
			g_engine->applyGameSettings();
			g_engine->syncSoundSettings();
		}
	}

protected:
	void build() override {
		GUI::GlobalOptionsDialog::build();

		if (_guiReturnToLauncherAtExit) {
			_guiReturnToLauncherAtExit->setState(false);
			_guiReturnToLauncherAtExit->setEnabled(false);
			_guiReturnToLauncherAtExit->setVisible(false);
		}

		if (g_engine && _tabWidget) {
			const Common::String &gameDomain = ConfMan.getActiveDomainName();
			const MetaEngine *metaEngine = g_engine->getMetaEngine();

			if (metaEngine && !gameDomain.empty()) {
				int tabId = _tabWidget->addTab(_("Game"), "GlobalConfig_Engine");
				GUI::ScrollContainerWidget *engineContainer = new GUI::ScrollContainerWidget(
					_tabWidget, "GlobalConfig_Engine.Container", "GlobalConfig_Engine_Container");
				engineContainer->setBackgroundType(GUI::ThemeEngine::kWidgetBackgroundNo);
				engineContainer->setTarget(this);

				_engineOptions = metaEngine->buildEngineOptionsWidget(
					engineContainer, "GlobalConfig_Engine_Container.Container", gameDomain);

				if (_engineOptions) {
					_engineOptions->setParentDialog(this);
					_engineOptions->load();
				} else {
					_tabWidget->removeTab(tabId);
				}
			}
		}
	}

	void clean() override {
		_engineOptions = nullptr;
		GUI::GlobalOptionsDialog::clean();
	}

private:
	GUI::OptionsContainerWidget *_engineOptions;
};

static bool matchesExpectedGame(const DetectedGame &dg, const GameConfig &cfg) {
	if (cfg.gameId[0] != '\0' && !dg.gameId.equalsIgnoreCase(cfg.gameId))
		return false;

	if (cfg.expectedPlatform[0] != '\0') {
		Common::Platform expectedPlat = Common::parsePlatform(cfg.expectedPlatform);
		if (expectedPlat != Common::kPlatformUnknown && dg.platform != expectedPlat)
			return false;
	}

	if (cfg.expectedLanguage[0] != '\0') {
		Common::Language expectedLang = Common::parseLanguage(cfg.expectedLanguage);
		if (expectedLang != Common::UNK_LANG && dg.language != expectedLang)
			return false;
	}

	if (cfg.expectedExtra[0] != '\0' && !dg.extra.equalsIgnoreCase(cfg.expectedExtra))
		return false;

	return true;
}

} // End of anonymous namespace

const GameConfig &getConfig() {
	return kGameConfig;
}

void initializeDefaults() {
	const Common::String &appDomain = Common::ConfigManager::kApplicationDomain;

	if (!ConfMan.hasKey("gfx_mode", appDomain))
		ConfMan.set("gfx_mode", "opengl", appDomain);
	if (!ConfMan.hasKey("stretch_mode", appDomain))
		ConfMan.set("stretch_mode", "fit", appDomain);
	if (!ConfMan.hasKey("aspect_ratio", appDomain))
		ConfMan.setBool("aspect_ratio", true, appDomain);
	if (!ConfMan.hasKey("filtering", appDomain))
		ConfMan.setBool("filtering", true, appDomain);
	if (!ConfMan.hasKey("vsync", appDomain))
		ConfMan.setBool("vsync", true, appDomain);
	if (!ConfMan.hasKey("gui_browser_native", appDomain))
		ConfMan.setBool("gui_browser_native", true, appDomain);
	if (!ConfMan.hasKey("gui_language", appDomain))
		ConfMan.set("gui_language", "pt_BR", appDomain);

	ConfMan.setBool("gui_return_to_launcher_at_exit", false, appDomain);
	ConfMan.registerDefault("enable_unsupported_game_warning", false);
}

bool validateIso(const Common::Path &isoPath, DetectedGame &matchedGame, Common::U32String &errorMessage) {
	const GameConfig &cfg = getConfig();
	Common::String nativePath = isoPath.toString(Common::Path::kNativeSeparator);

	if (nativePath.empty()) {
		errorMessage = utf8Str("Nenhum arquivo ISO foi selecionado.");
		return false;
	}

	if (!hostFileExists(nativePath)) {
		errorMessage = utf8Str("O arquivo ISO especificado n\xc3\xa3o foi encontrado:\n") + utf8Str(nativePath.c_str());
		return false;
	}

	Common::Iso9660Image::evictCached(nativePath);
	Common::SharedPtr<Common::Iso9660Image> image = Common::Iso9660Image::openCached(nativePath);
	if (!image) {
		errorMessage = utf8Str("O arquivo selecionado n\xc3\xa3o \xc3\xa9 uma imagem ISO 9660 v\xc3\xa1lida ou est\xc3\xa1 corrompido.");
		return false;
	}

	Common::FSNode isoRoot(isoPath);
	if (!isoRoot.exists() || !isoRoot.isDirectory()) {
		errorMessage = utf8Str("N\xc3\xa3o foi poss\xc3\xadvel abrir o sistema de arquivos da imagem ISO selecionada.");
		return false;
	}

	Common::FSList fslist;
	if (!isoRoot.getChildren(fslist, Common::FSNode::kListAll)) {
		errorMessage = utf8Str("N\xc3\xa3o foi poss\xc3\xadvel listar os arquivos dentro da imagem ISO selecionada.");
		return false;
	}

	const Plugin *detectionPlugin = EngineMan.findDetectionPlugin(cfg.engineId);
	if (!detectionPlugin) {
		errorMessage = utf8Str("Erro interno: motor de detec\xc3\xa7\xc3\xa3o n\xc3\xa3o encontrado.");
		return false;
	}

	MetaEngineDetection &metaEngine = detectionPlugin->get<MetaEngineDetection>();
	DetectedGames detected = metaEngine.detectGames(fslist);

	for (uint i = 0; i < detected.size(); ++i) {
		if (matchesExpectedGame(detected[i], cfg)) {
			matchedGame = detected[i];
			return true;
		}
	}

	if (!detected.empty()) {
		Common::String foundDesc = detected[0].description;
		if (foundDesc.empty())
			foundDesc = detected[0].gameId;
		errorMessage = utf8Str("A ISO selecionada n\xc3\xa3o corresponde \xc3\xa0 vers\xc3\xa3o suportada de ") +
			utf8Str(cfg.displayName) +
			utf8Str(".\n\nConte\xc3\xba""do detectado na ISO:\n") +
			utf8Str(foundDesc.c_str());
	} else {
		errorMessage = utf8Str("A ISO selecionada n\xc3\xa3o corresponde a ") +
			utf8Str(cfg.displayName) +
			utf8Str(".\nVerifique se voc\xc3\xaa selecionou a imagem ISO original da vers\xc3\xa3o brasileira suportada.");
	}

	Common::Iso9660Image::evictCached(nativePath);
	return false;
}

void configureTargetDomain(const Common::Path &isoPath, const DetectedGame &matchedGame) {
	const GameConfig &cfg = getConfig();
	const Common::String target(cfg.targetName);

	if (!ConfMan.hasGameDomain(target))
		ConfMan.addGameDomain(target);

	ConfMan.set("engineid", cfg.engineId, target);
	ConfMan.set("gameid", matchedGame.gameId, target);
	ConfMan.set("description", cfg.displayName, target);
	ConfMan.setPath("path", isoPath, target);

	if (matchedGame.platform != Common::kPlatformUnknown)
		ConfMan.set("platform", Common::getPlatformCode(matchedGame.platform), target);
	if (matchedGame.language != Common::UNK_LANG)
		ConfMan.set("language", Common::getLanguageCode(matchedGame.language), target);
	if (!matchedGame.extra.empty())
		ConfMan.set("extra", matchedGame.extra, target);
	else
		ConfMan.removeKey("extra", target);

	Common::String guio = matchedGame.getGUIOptions();
	if (!guio.empty())
		ConfMan.set("guioptions", guio, target);

	ConfMan.setBool("enable_unsupported_game_warning", false, target);
	ConfMan.setActiveDomain(target);
	ConfMan.flushToDisk();
}

bool selectNewIsoInteractive(bool isReselect) {
	const GameConfig &cfg = getConfig();
	Common::U32String statusNote;

	if (!isReselect && ConfMan.hasGameDomain(cfg.targetName) && ConfMan.hasKey("path", cfg.targetName)) {
		Common::Path prev = ConfMan.getPath("path", cfg.targetName);
		if (!prev.empty()) {
			statusNote = utf8Str("\n\nISO anterior indispon\xc3\xadvel ou inv\xc3\xa1lida:\n") +
				utf8Str(prev.toString(Common::Path::kNativeSeparator).c_str());
		}
	}

	while (true) {
		if (!isReselect) {
			Common::U32String msg = utf8Str(cfg.displayName) +
				utf8Str("\n\nEste execut\xc3\xa1vel n\xc3\xa3o cont\xc3\xa9m os arquivos originais do jogo.\n"
				        "Selecione a imagem ISO original do seu CD para iniciar.\n\n"
				        "Dica: durante o jogo, pressione F5 para abrir o menu e configura\xc3\xa7\xc3\xb5""es.") +
				statusNote;

			Common::U32StringArray altButtons;
			altButtons.push_back(utf8Str("Configura\xc3\xa7\xc3\xb5""es..."));
			altButtons.push_back(utf8Str("Sair"));

			GUI::MessageDialog welcome(msg, utf8Str("Selecionar ISO..."), altButtons);
			int res = welcome.runModal();
			if (res == GUI::kMessageAlt) {
				openOptionsDialog();
				continue;
			} else if (res != GUI::kMessageOK) {
				return false;
			}
		}

		Common::Path chosenIso;
		if (!pickIsoFile(chosenIso)) {
			if (isReselect)
				return false;
			continue;
		}

		DetectedGame matchedGame;
		Common::U32String errMsg;
		if (validateIso(chosenIso, matchedGame, errMsg)) {
			configureTargetDomain(chosenIso, matchedGame);
			return true;
		}

		GUI::MessageDialog errorDlg(errMsg, utf8Str("OK"));
		errorDlg.runModal();
		if (isReselect)
			return false;
		statusNote.clear();
	}
}

bool ensureGameReady(const Common::StringMap &cliSettings) {
	const GameConfig &cfg = getConfig();
	const Common::String target(cfg.targetName);

	// Allow explicitly specifying an ISO via --iso=<path>
	if (cliSettings.contains("iso")) {
		Common::Path isoPath(cliSettings.getVal("iso"), Common::Path::kNativeSeparator);
		DetectedGame matchedGame;
		Common::U32String errMsg;
		if (validateIso(isoPath, matchedGame, errMsg)) {
			configureTargetDomain(isoPath, matchedGame);
			return true;
		}
		GUI::MessageDialog errorDlg(errMsg, utf8Str("OK"));
		errorDlg.runModal();
		return selectNewIsoInteractive(false);
	}

	// Allow forcing ISO re-selection via --select-iso
	if (cliSettings.contains("select-iso")) {
		return selectNewIsoInteractive(false);
	}

	// Check saved ISO path from previous executions
	if (ConfMan.hasGameDomain(target) && ConfMan.hasKey("path", target)) {
		Common::Path savedIso = ConfMan.getPath("path", target);
		DetectedGame matchedGame;
		Common::U32String errMsg;
		if (validateIso(savedIso, matchedGame, errMsg)) {
			configureTargetDomain(savedIso, matchedGame);
			return true;
		}
		warning("Standalone: Saved ISO failed validation: %s", errMsg.encode(Common::kUtf8).c_str());
	}

	return selectNewIsoInteractive(false);
}

void openOptionsDialog() {
	StandaloneOptionsDialog dlg;
	dlg.runModal();
}

void applyWindowBranding() {
	const GameConfig &cfg = getConfig();
	if (g_system) {
		g_system->setWindowCaption(utf8Str(cfg.displayName));
	}

#if defined(WIN32)
	if (cfg.runtimeIconFile[0] != '\0' && ConfMan.hasGameDomain(cfg.targetName) && ConfMan.hasKey("path", cfg.targetName)) {
		OSystem_Win32 *win32Sys = dynamic_cast<OSystem_Win32 *>(g_system);
		if (win32Sys) {
			HWND hwnd = win32Sys->getWindowHandle();
			Common::String isoPath = ConfMan.getPath("path", cfg.targetName).toString(Common::Path::kNativeSeparator);
			if (hwnd && !isoPath.empty()) {
				Win32::applyWindowIconFromIso(hwnd, isoPath, cfg.runtimeIconFile);
			}
		}
	}
#endif
}

void requestRestart() {
	g_restartRequested = true;
}

bool consumeRestartRequested() {
	bool was = g_restartRequested;
	g_restartRequested = false;
	return was;
}

int runValidateIsoCommand(const Common::String &isoPathStr) {
	Common::Path isoPath(isoPathStr, Common::Path::kNativeSeparator);
	DetectedGame matchedGame;
	Common::U32String errMsg;
	if (validateIso(isoPath, matchedGame, errMsg)) {
		printf("VALID_ISO: target=%s engine=%s game=%s lang=%s platform=%s extra=%s\n",
			getConfig().targetName,
			getConfig().engineId,
			matchedGame.gameId.c_str(),
			Common::getLanguageCode(matchedGame.language),
			Common::getPlatformCode(matchedGame.platform),
			matchedGame.extra.c_str());
		return 0;
	}
	printf("INVALID_ISO: %s\n", errMsg.encode(Common::kUtf8).c_str());
	return 1;
}

} // End of namespace Standalone

#endif // SCUMMVM_STANDALONE
