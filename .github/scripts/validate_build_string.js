if (!['Debug', 'Relwithdebinfo', 'Release'].includes(process.env.buildtype)) {
    console.log("build string must be either 'Debug', 'Relwithdebinfo' or 'Release'");
    process.exit(1);
  }
  