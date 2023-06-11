from xml.etree import ElementTree as ET
import logging


CONFIG_DEBUG_PREFIX = "com.renesas.cdt.managedbuild.gcc.rz.configuration.debug."

# Referenced
# https://github.com/eclipse-embed-cdt/eclipse-plugins/blob/752ae80fe82e14770728ebd18626de9d183ca625/plugins/org.eclipse.embedcdt.managedbuild.cross.arm.core/plugin.xml#L156
SUPERCLASS_OPT_MAPPING = {
    "com.renesas.cdt.managedbuild.gcc.rz.option.optimizationLevel": {
        "ilg.gnuarmeclipse.managedbuild.cross.option.optimization.level.none": "-O0",
        "ilg.gnuarmeclipse.managedbuild.cross.option.optimization.level.optimize": "-O1",
        "ilg.gnuarmeclipse.managedbuild.cross.option.optimization.level.more": "-O2",
        "ilg.gnuarmeclipse.managedbuild.cross.option.optimization.level.most": "-O3",
        "ilg.gnuarmeclipse.managedbuild.cross.option.optimization.level.size": "-Os",
        "ilg.gnuarmeclipse.managedbuild.cross.option.optimization.level.fast": "-Ofast",
        "ilg.gnuarmeclipse.managedbuild.cross.option.optimization.level.debug": "-Og",
    },
    "ilg.gnuarmeclipse.managedbuild.cross.option.debugging.level": {
        "ilg.gnuarmeclipse.managedbuild.cross.option.debugging.level.none": "",
        "ilg.gnuarmeclipse.managedbuild.cross.option.debugging.level.minimal": "-g1",
        "ilg.gnuarmeclipse.managedbuild.cross.option.debugging.level.default": "-g",
        "ilg.gnuarmeclipse.managedbuild.cross.option.debugging.level.max": "-g3",
    },
    "ilg.gnuarmeclipse.managedbuild.cross.option.architecture": {
        "ilg.gnuarmeclipse.managedbuild.cross.option.architecture.arm": ""
    },
    "com.renesas.cdt.managedbuild.gcc.rz.option.family": {
        "ilg.gnuarmeclipse.managedbuild.cross.option.arm.target.mcpu.cortex-a9": "-mcpu=cortex-a9"
    },
    "com.renesas.cdt.managedbuild.gcc.rz.option.instructionset": {
        "ilg.gnuarmeclipse.managedbuild.cross.option.arm.target.instructionset.arm": "-marm"
    },
    "com.renesas.cdt.managedbuild.gcc.rz.option.architecture": {
        "ilg.gnuarmeclipse.managedbuild.cross.option.arm.target.arch.armv7-a": "-march=armv7-a"
    },
    "com.renesas.cdt.managedbuild.gcc.rz.option.floatunit": {
        "ilg.gnuarmeclipse.managedbuild.cross.option.arm.target.fpu.unit.neon": "-mfpu=neon"
    },
    "com.renesas.cdt.managedbuild.gcc.rz.option.floatabi": {
        "ilg.gnuarmeclipse.managedbuild.cross.option.arm.target.fpu.abi.hard": "-mfloat-abi=hard"
    },
    "com.renesas.cdt.managedbuild.gcc.rz.option.endian": {
        "ilg.gnuarmeclipse.managedbuild.cross.option.arm.target.endianness.little": "-mlittle-endian",
        "ilg.gnuarmeclipse.managedbuild.cross.option.arm.target.endianness.big": "-mbig-endian",
    },
    "com.renesas.cdt.managedbuild.gcc.rz.option.thumb": {
        "true": "-mthumb-interwork",
    },
    "com.renesas.cdt.managedbuild.gcc.rz.option.freestanding": {
        "true": "-ffreestanding"
    },
    "com.renesas.cdt.managedbuild.gcc.rz.option.messagelenght": {
        "true": "-fmessage-length=0"
    },
    "com.renesas.cdt.managedbuild.gcc.rz.option.signedchar": {"true": "-fsigned-char"},
    "com.renesas.cdt.managedbuild.gcc.rz.option.functionsection": {
        "true": "-ffunction-sections"
    },
    "com.renesas.cdt.managedbuild.gcc.rz.option.datasections": {
        "true": "-fdata-sections"
    },
    "ilg.gnuarmeclipse.managedbuild.cross.option.toolchain.name": {},
}


class EclipseProject:
    def __init__(self, logger: logging.Logger):
        self._log = logger.getChild("EclipseProject")
        self._log.info("Loading XML...")
        self._tree = ET.parse(".cproject")
        self._root = self._tree.getroot()
        self._configs = self._extract_debug_data()
        # self._log.info("Final config setup: {}".format(self._configs))

        cl = self.generate_build_commands("e2-build-release-oled")
        self._log.info(cl)

    def _extract_debug_data(self):
        debug_id_dict = {}
        for config in self._root.findall(
            ".//storageModule[@moduleId='org.eclipse.cdt.core.settings']/cconfiguration"
        ):
            debug_id_dict[config.attrib.get("id").replace(CONFIG_DEBUG_PREFIX, "")] = {
                "node": config,
                "build_opts": self._interpret_build_opts(config),
            }
        return debug_id_dict

    def _interpret_build_opts(self, node):
        build_dict = {}

        for module in node.findall(
            ".//storageModule[@moduleId='cdtBuildSystem']/configuration"
        ):
            self._log.info(
                "Found build config for: {}".format(module.attrib.get("name"))
            )
            build_dict[module.attrib.get("name")] = {
                "node": module,
                "description": module.attrib.get("description"),
                "clean_command": module.attrib.get("cleanCommand"),
                "postbuild_step": module.attrib.get("postbuildStep"),
                "toolchain_options": self._interpret_toolchain_opts(module),
            }
        return build_dict

    def _interpret_toolchain_opts(self, node):
        toolchain_opts = {}

        for option in node.findall(".//toolChain/option"):
            toolchain_opts[option.attrib.get("superClass")] = {
                "value": option.attrib.get("value"),
                "value_type": option.attrib.get("valueType"),
            }
            if option.attrib.get("valueType") in ("enumerated", "boolean"):
                toolchain_opts[option.attrib.get("superClass")]["command"] = (
                    self.translate_build_opt_to_argument(
                        option.attrib.get("superClass"), option.attrib.get("value")
                    ),
                )

        return toolchain_opts

    def translate_build_opt_to_argument(self, super_class, value):
        mapping = SUPERCLASS_OPT_MAPPING.get(super_class)

        if mapping:
            mval = mapping.get(value)
            if mval:
                return mval

        return ""

    def generate_build_commands(self, target):
        commandline = ""
        for config, data in self._configs.items():
            bo = data.get("build_opts")
            targ = bo.get(target)

        return commandline
