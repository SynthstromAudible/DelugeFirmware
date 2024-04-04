if (!['debug', 'relwithdebinfo', 'release'].includes(process.env.buildtype)) {
    console.log("build string must be either 'debug', 'relwithdebinfo' or 'release'");
    process.exit(1);
  }
  