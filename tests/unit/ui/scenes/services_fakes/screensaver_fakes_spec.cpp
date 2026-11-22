/// @file
/// @brief The screensaver overlay (`overlay/screensaver_scene.cpp`) over the device settings' fake: the mode it reads
///        from the `ui/screensaver/mode` row, when it reads it, and what it reads with no device port installed.

#include "ui/scenes/overlay/screensaver_scene.h"

#include "project/device_settings.h"
#include "ui/shell/scene.h"

#include "fakes.h"

#include "cppspec.hpp"

#include <cstdint>
#include <optional>
#include <utility>

#include "support/spec_main.h"

using Mode = deluge::project::ScreensaverMode;
using deluge::project::device_path;
using deluge::ui::scenes::bind::test::FakeDeviceSettings;
using deluge::ui::scenes::overlay::kFrameIntervalMs;
using deluge::ui::scenes::overlay::kScreensaverSceneKind;
using deluge::ui::scenes::overlay::ScreensaverScene;
using deluge::ui::shell::SceneId;

namespace {

/// @brief Write @p mode to the fake's screensaver row.
void set_mode(FakeDeviceSettings& device, Mode mode) {
	(void)device.write(device_path::kScreensaverMode, std::to_underlying(mode));
}

} // namespace

// clang-format off
describe screensaver_fakes("overlay::ScreensaverScene over the device settings' fake", ${
	it("reads the mode row as it is entered: STARSCAPE animates, OFF asks for no frame", _ {
		FakeDeviceSettings device;
		(void)device.write(device_path::kScreensaverMode, 2);
		ScreensaverScene scene;
		scene.set_device(&device);
		expect(scene.on_enter(SceneId{kScreensaverSceneKind, 0})).to_be_true();
		expect(scene.mode()).to_equal(Mode::STARSCAPE);
		expect(scene.next_timer()).to_equal(std::optional<uint32_t>{kFrameIntervalMs});

		set_mode(device, Mode::OFF);
		expect(scene.on_enter(SceneId{kScreensaverSceneKind, 0})).to_be_true();
		expect(scene.mode()).to_equal(Mode::OFF);
		expect(scene.next_timer().has_value()).to_be_false();
	});

	it("reads the mode row again on every tick", _ {
		FakeDeviceSettings device;
		set_mode(device, Mode::STARSCAPE);
		ScreensaverScene scene;
		scene.set_device(&device);
		expect(scene.on_enter(SceneId{kScreensaverSceneKind, 0})).to_be_true();
		set_mode(device, Mode::BLANK);
		// Until the tick, the scene answers the mode it entered with.
		expect(scene.mode()).to_equal(Mode::STARSCAPE);
		scene.tick(kFrameIntervalMs);
		expect(scene.mode()).to_equal(Mode::BLANK);
		expect(scene.next_timer().has_value()).to_be_false();
	});

	it("starts at OFF, and reads the row's default, DELUGE, with no device port", _ {
		ScreensaverScene scene;
		expect(scene.mode()).to_equal(Mode::OFF);
		expect(scene.on_enter(SceneId{kScreensaverSceneKind, 0})).to_be_true();
		expect(scene.mode()).to_equal(Mode::DELUGE);
		expect(scene.next_timer().has_value()).to_be_true();
	});
});
// clang-format on

DELUGE_SPEC(screensaver_fakes_spec, screensaver_fakes)
