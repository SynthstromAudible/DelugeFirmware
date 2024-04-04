if (!['Debug', 'Relwithdebinfo', 'Release'].includes(process.env.buildtype)) {
    console.log("build string must be either 'debug', 'relwithdebinfo' or 'release'");
    process.exit(1);
  }
  