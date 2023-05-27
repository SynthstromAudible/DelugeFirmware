import os
from dbt.util import path_as_posix

DefaultEnvironment(tools=[])
EnsurePythonVersion(3, 10)

# This environment is created only for loading options & validating file/dir existence
dbt_variables = SConscript("site_scons/commandline.scons")
cmd_environment = Environment(
    toolpath=["#/scripts/dbt_tools"],
    tools=[
        ("dbt_help", {"vars": dbt_variables}),
    ],
    variables=dbt_variables,
)

# Building basic environment - tools, utility methods, cross-compilation
# settings, gcc flags for Cortex-M4, basic builders and more
coreenv = SConscript(
    "site_scons/environ.scons",
    exports={"VAR_ENV": cmd_environment},
    toolpath=["#/scripts/dbt_tools"],
)
SConscript("site_scons/cc.scons", exports={"ENV": coreenv})

# Create a separate "dist" environment and add construction envs to it
distenv = coreenv.Clone(
    tools=[
        "dbt_dist",
        "dbt_debugopts",
        "openocd",
        # "blackmagic",
        # "jflash",
    ],
    ENV=os.environ,
    UPDATE_BUNDLE_DIR="dist/${DIST_DIR}/${HW_VERSION}-update-${DIST_SUFFIX}",
)

firmware_env = distenv.AddFwProject(
    base_env=coreenv,
    fw_type="firmware",
    fw_env_key="FW_ENV",
)

# Cross-compilation settings
coreenv = SConscript(
    "site_scons/environ.scons",
    exports={"VAR_ENV": cmd_environment},
    toolpath=["#/scripts/dbt_tools"],
)
SConscript("site_scons/cc.scons", exports={"ENV": coreenv})

firmware_env.Append(
    PY_LINT_SOURCES=[
        # Py code folders
        "site_scons",
        "scripts",
        # Extra files
        "SConstruct",
        "firmware.scons",
        "dbt_options.py",
    ]
)