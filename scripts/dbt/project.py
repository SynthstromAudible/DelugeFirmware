import os
from SCons.Script import *
from xml.etree import ElementTree as ET

CONFIG_DEBUG_PREFIX = "com.renesas.cdt.managedbuild.gcc.rz.configuration.debug."

# Referenced
# https://github.com/eclipse-embed-cdt/eclipse-plugins/blob/752ae80fe82e14770728ebd18626de9d183ca625/plugins/org.eclipse.embedcdt.managedbuild.cross.arm.core/plugin.xml#L156
SUPERCLASS_OPT_MAPPING = {
    "ilg.gnuarmeclipse.managedbuild.cross.option.debugging.format": {
        "ilg.gnuarmeclipse.managedbuild.cross.option.debugging.format.gdb": "-ggdb",
        "ilg.gnuarmeclipse.managedbuild.cross.option.debugging.format.stabs": "-gstabs",
        "ilg.gnuarmeclipse.managedbuild.cross.option.debugging.format.stabsplus": "-gstabs+",
        "ilg.gnuarmeclipse.managedbuild.cross.option.debugging.format.dwarf2": "-gdwarf-2",
        "ilg.gnuarmeclipse.managedbuild.cross.option.debugging.format.dwarf3": "-gdwarf-3",
        "ilg.gnuarmeclipse.managedbuild.cross.option.debugging.format.dwarf4": "-gdwarf-4",
        "ilg.gnuarmeclipse.managedbuild.cross.option.debugging.format.dwarf5": "-gdwarf-5",
    },
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
    # Commenting out the following to override e2studio and remove some warnings.
    # "com.renesas.cdt.managedbuild.gcc.rz.option.architecture": {
    #     "ilg.gnuarmeclipse.managedbuild.cross.option.arm.target.arch.armv7-a": "-march=armv7-a"
    # },
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
    "com.renesas.cdt.managedbuild.gcc.rz.createlibgen": {},
    "com.renesas.cdt.managedbuild.gcc.rz.option.warnings.nulldereference": {
        "true": "-Wnull-dereference"
    },
    "ilg.gnuarmeclipse.managedbuild.cross.option.optimization.lto": {"true": "-flto"},
    # "ilg.gnuarmeclipse.managedbuild.cross.option.cpp.compiler.nortti": {
    #     "true": "-fno-rtti"
    # },
    # "ilg.gnuarmeclipse.managedbuild.cross.option.cpp.compiler.noexceptions": {
    #     "true": "-fno-exceptions"
    # },
    # "ilg.gnuarmeclipse.managedbuild.cross.option.cpp.compiler.nothreadsafestatics": {
    #     "true": "-fno-threadsafe-statics"
    # },
    # "ilg.gnuarmeclipse.managedbuild.cross.option.cpp.compiler.nousecxaatexit": {
    #     "true": "-fno-use-cxa-atexit"
    # },
    "ilg.gnuarmeclipse.managedbuild.cross.option.cpp.linker.gcsections": {
        "true": "-Xlinker --gc-sections"
    },
    "ilg.gnuarmeclipse.managedbuild.cross.option.cpp.linker.strip": {"true": "-s"},
    "ilg.gnuarmeclipse.managedbuild.cross.option.cpp.linker.nodeflibs": {
        "true": "-nodefaultlibs"
    },
    "ilg.gnuarmeclipse.managedbuild.cross.option.cpp.linker.nostdlibs": {
        "true": "-nostdlib"
    },
    "ilg.gnuarmeclipse.managedbuild.cross.option.createflash.choice": {
        "ilg.gnuarmeclipse.managedbuild.cross.option.createflash.choice.ihex": "-O ihex",
        "ilg.gnuarmeclipse.managedbuild.cross.option.createflash.choice.srec": "-O srec",
        "ilg.gnuarmeclipse.managedbuild.cross.option.createflash.choice.symbolsrec": "-O symbolsrec",
        "ilg.gnuarmeclipse.managedbuild.cross.option.createflash.choice.binary": "-O binary",
    },
}

SUPERCLASS_SETTABLE_COMMAND_MAP = {
    "com.renesas.cdt.managedbuild.gcc.rz.option.warnStack": {
        "command": "-Wstack-usage={}"
    },
    # "com.renesas.cdt.managedbuild.gcc.rz.option.linker.cpp.entrypoint": {
    #     "command": "{}"
    # },
}

SUPERCLASS_TOOL_OPTS = {
    "ilg.gnuarmeclipse.managedbuild.cross.option.command.prefix": {"PREFIX": "{}"},
    "ilg.gnuarmeclipse.managedbuild.cross.option.command.c": {"CC": "{}"},
    "ilg.gnuarmeclipse.managedbuild.cross.option.command.cpp": {"CXX": "{}"},
    "ilg.gnuarmeclipse.managedbuild.cross.option.command.ar": {"AR": "{}"},
    "ilg.gnuarmeclipse.managedbuild.cross.option.command.objcopy": {"OBJCOPY": "{}"},
    "ilg.gnuarmeclipse.managedbuild.cross.option.command.objdump": {"OBJDUMP": "{}"},
    "ilg.gnuarmeclipse.managedbuild.cross.option.command.size": {"SIZE": "{}"},
    "ilg.gnuarmeclipse.managedbuild.cross.option.command.make": {"MAKE": "{}"},
    "ilg.gnuarmeclipse.managedbuild.cross.option.command.rm": {"RM": "{}"},
}

MACRO_VALUES = {
    "${ProjName}": "Deluge",
    "${ProjDirPath}": ".",
    "&quot;": "",
    '"': "",
}


class E2Project:
    def __init__(self, project_path="."):
        self._tree = ET.parse(os.path.join(project_path, ".cproject"))
        self._root = self._tree.getroot()
        self._current_build = ""
        self._builds = {}
        self._configs = self._extract_debug_data()

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
        for module in node.findall(
            ".//storageModule[@moduleId='cdtBuildSystem']/configuration"
        ):
            self._current_build = module.attrib.get("name")
            self._builds[module.attrib.get("name")] = {
                "node": module,
                "artifact_name": self.rmacro(module.attrib.get("artifactName")),
                "artifact_extension": ".{}".format(
                    module.attrib.get("artifactExtension", "elf")
                ),
                "description": module.attrib.get("description"),
                "clean_command": module.attrib.get("cleanCommand"),
                "postbuild_step": module.attrib.get("postbuildStep"),
                "toolchain_options": self._interpret_toolchain_opts(module),
                "tools": self._interpret_toolchain_tools(module),
                "tool_asm": self._interpret_tool_asm(module),
                "tool_c": self._interpret_tool_c(module),
                "tool_cpp": self._interpret_tool_cpp(module),
                "tool_link": self._interpret_tool_link(module),
                "tool_ar": self._interpret_tool_ar(module),
                "tool_flash": self._interpret_tool_flash(module),
            }
        return self._builds

    def rmacro(self, input: str):
        output = input
        for key, val in MACRO_VALUES.items():
            output = output.replace(key, val)
            if self._current_build:
                output = output.replace("${build_project}", self._current_build)

        return output

    def _interpret_tool_asm(self, node):
        opts = {}

        for option in node.findall(
            ".//tool[@superClass='com.renesas.cdt.managedbuild.gcc.rz.tool.assembler']/option"
        ):
            if (
                option.attrib.get("superClass")
                == "ilg.gnuarmeclipse.managedbuild.cross.option.assembler.usepreprocessor"
            ):
                opts["use_preprocessor"] = option.attrib.get("value", False)
            if (
                option.attrib.get("superClass")
                == "com.renesas.cdt.managedbuild.gcc.rz.option.assembler.include"
            ):
                opts["scanner_discovery"] = (
                    True if option.attrib.get("useByScannerDiscovery") else False
                )
                opts["include_path"] = []
                for entry in option:
                    opts["include_path"].append(self.rmacro(entry.attrib.get("value")))

        return opts

    def _interpret_tool_c(self, node):
        opts = {}

        for option in node.findall(
            ".//tool[@superClass='com.renesas.cdt.managedbuild.gcc.rz.tool.compiler.c']/option"
        ):
            if (
                option.attrib.get("superClass")
                == "com.renesas.cdt.managedbuild.gcc.rz.option.compiler.include"
            ):
                opts["scanner_discovery"] = (
                    True if option.attrib.get("useByScannerDiscovery") else False
                )
                opts["include_path"] = []
                for entry in option:
                    opts["include_path"].append(self.rmacro(entry.attrib.get("value")))

        return opts

    def _interpret_tool_cpp(self, node):
        opts = {}
        opts["command"] = []
        opts["defs"] = []
        opts["include_path"] = []
        opts["other_optimization"] = []

        for option in node.findall(
            ".//tool[@superClass='com.renesas.cdt.managedbuild.gcc.rz.tool.compiler.cpp']/option"
        ):
            if (
                option.attrib.get("superClass")
                == "com.renesas.cdt.managedbuild.gcc.rz.option.compiler.cpp.include"
            ):
                opts["scanner_discovery"] = (
                    True if option.attrib.get("useByScannerDiscovery") else False
                )
                for entry in option:
                    opts["include_path"].append(self.rmacro(entry.attrib.get("value")))
            elif (
                option.attrib.get("superClass")
                == "ilg.gnuarmeclipse.managedbuild.cross.option.cpp.compiler.defs"
            ):
                for ppdef in option:
                    opts["defs"].append(ppdef.attrib.get("value"))
            elif (
                option.attrib.get("superClass")
                == "com.renesas.cdt.managedbuild.gcc.rz.option.compiler.cpp.otherOptimization"
            ):
                for oopt in option:
                    opts["other_optimization"].append(oopt.attrib.get("value"))
            else:
                opts["command"].append(
                    self.translate_build_opt_to_command(
                        option.attrib.get("superClass"),
                        option.attrib.get("value"),
                        option.attrib.get("valueType"),
                    )
                )

        return opts

    def _interpret_tool_link(self, node):
        opts = {}
        opts["include_file"] = []
        opts["include_path"] = []
        opts["linkage_order"] = []
        opts["other_objs"] = []
        opts["script_files"] = []
        opts["command"] = []

        for option in node.findall(
            ".//tool[@superClass='com.renesas.cdt.managedbuild.gcc.rz.tool.linker.cpp']/option"
        ):
            if (
                option.attrib.get("superClass")
                == "com.renesas.cdt.managedbuild.gcc.rz.archives.cpp.includeFiles"
            ):
                for entry in option:
                    opts["include_file"].append(self.rmacro(entry.attrib.get("value")))
            elif (
                option.attrib.get("superClass")
                == "com.renesas.cdt.managedbuild.gcc.rz.archives.cpp.includePath"
            ):
                for entry in option:
                    opts["include_path"].append(self.rmacro(entry.attrib.get("value")))
            elif (
                option.attrib.get("superClass")
                == "com.renesas.cdt.managedbuild.gcc.rz.cpp.option.linkageOrder"
            ):
                for entry in option:
                    opts["linkage_order"].append(self.rmacro(entry.attrib.get("value")))
            elif (
                option.attrib.get("superClass")
                == "ilg.gnuarmeclipse.managedbuild.cross.option.cpp.linker.otherobjs"
            ):
                for entry in option:
                    opts["other_objs"].append(self.rmacro(entry.attrib.get("value")))
            elif (
                option.attrib.get("superClass")
                == "com.renesas.cdt.managedbuild.gcc.rz.option.linker.cpp.userDefined"
            ):
                for entry in option:
                    opts["command"].append(self.rmacro(entry.attrib.get("value")))
            elif (
                option.attrib.get("superClass")
                == "com.renesas.cdt.managedbuild.gcc.rz.option.cpp.linkerscript"
            ):
                for entry in option:
                    opts["script_files"].append(self.rmacro(entry.attrib.get("value")))
            else:
                opts["command"].append(
                    self.translate_build_opt_to_command(
                        option.attrib.get("superClass"),
                        option.attrib.get("value"),
                        option.attrib.get("valueType"),
                    )
                )

        return opts

    def _interpret_tool_ar(self, node):
        opts = {}

        for option in node.findall(
            ".//tool[@superClass='com.renesas.cdt.managedbuild.gcc.rz.tool.archiver']/option"
        ):
            continue

        return opts

    def _interpret_tool_flash(self, node):
        opts = {}
        opts["command"] = []

        for option in node.findall(
            ".//tool[@superClass='com.renesas.cdt.managedbuild.gcc.rz.tool.flash']/option"
        ):
            opts["command"].append(
                self.translate_build_opt_to_command(
                    option.attrib.get("superClass"),
                    option.attrib.get("value"),
                    option.attrib.get("valueType"),
                )
            )

        return opts

    def _interpret_toolchain_tools(self, node):
        toolchain_tools = {}

        for option in node.findall(".//toolChain/option"):
            mapping = SUPERCLASS_TOOL_OPTS.get(option.attrib.get("superClass"))
            if mapping and option.attrib.get("valueType") == "string":
                for name, tool in mapping.items():
                    toolchain_tools[name] = tool.format(option.attrib.get("value", ""))

        return toolchain_tools

    def _interpret_toolchain_opts(self, node):
        toolchain_opts = {}

        for option in node.findall(".//toolChain/option"):
            toolchain_opts[option.attrib.get("superClass")] = {
                "value": option.attrib.get("value"),
                "value_type": option.attrib.get("valueType"),
            }
            if option.attrib.get("valueType"):
                toolchain_opts[option.attrib.get("superClass")][
                    "command"
                ] = self.translate_build_opt_to_command(
                    option.attrib.get("superClass"),
                    option.attrib.get("value"),
                    option.attrib.get("valueType"),
                )

        return toolchain_opts

    def translate_build_opt_to_command(self, super_class, value, val_type):
        mapping = SUPERCLASS_OPT_MAPPING.get(super_class)
        if mapping and val_type in ("enumerated", "boolean"):
            mval = mapping.get(value)
            if mval:
                return mval

        mapping = SUPERCLASS_SETTABLE_COMMAND_MAP.get(super_class)
        if mapping and val_type in ("string"):
            mval = mapping.get("command")
            if mval:
                return mval.format(value)

        return ""

    def get_build_targets(self):
        return self._builds.keys()

    def get_target_ext(self, target):
        return self._builds[target].get("artifact_extension")

    def get_target_filename(self, target):
        return self._builds[target].get("artifact_name")

    def get_asm_includes(self, target):
        return self._builds[target]["tool_asm"]["include_path"]

    def get_c_includes(self, target):
        return self._builds[target]["tool_c"]["include_path"]

    def get_cpp_includes(self, target):
        return self._builds[target]["tool_cpp"]["include_path"]

    def get_build_args(self, target):
        commands = []
        targ_data = self._builds.get(target)
        if targ_data:
            for t_opt, vals in targ_data.get("toolchain_options", {}).items():
                if not vals.get("command"):
                    continue
                commands.append(vals.get("command"))

        return commands

    def get_preprocessor_defs(self, target):
        defs = self._builds[target]["tool_c"].get("defs", [])
        defs.extend(self._builds[target]["tool_cpp"].get("defs", []))

        return defs

    def get_c_flags(self, target):
        flags = self.get_build_args(target)

        flags.extend(self._builds[target]["tool_c"].get("command", []))

        return flags

    def get_cpp_flags(self, target):
        flags = self.get_build_args(target)

        flags.extend(self._builds[target]["tool_cpp"].get("command", []))

        return flags

    def get_link_flags(self, target):
        # flags = "{} ".format(" ".join(self.get_build_args(target)))
        flags = " ".join(
            [
                "-T {}".format(os.path.basename(script))
                for script in self._builds[target]["tool_link"].get("script_files", [])
            ]
        )

        flags += " -Wl,--start-group -Wl,--end-group -nostartfiles"

        flags += " {}".format(
            " ".join(self._builds[target]["tool_link"].get("command", []))
        )

        flags += ' -Wl,-Map,"$MAPFILE" -Wl,-estart --specs=rdimon.specs'

        return flags

    def get_link_libs_order(self, target):
        libs = self._builds[target]["tool_link"].get("linkage_order", [])

        return libs

    def get_toolchain_tools(self, target):
        tools = self._builds[target].get("tools", "")
        if not tools:
            return {}

        prefix = self._builds[target]["tools"].get("PREFIX", "")
        tools_map = {}

        root_path = os.environ.get("DBT_TOOLCHAIN_PATH", "")

        for tool, exec in tools.items():
            if tool == "PREFIX":
                continue
            elif tool in ["MAKE", "RM"]:
                tools_map[tool] = exec
            else:
                real_tool = "{}{}".format(prefix, exec)
                tools_map[tool] = real_tool

        # tools_map["LINK"] = "{}g++".format(prefix)

        return tools_map
