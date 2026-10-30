if (!['Debug', 'RelWithDebInfo', 'Release'].includes(process.env.buildtype)) {
    console.log("build string must be either 'Debug', 'RelWithDebInfo' or 'Release'");
    process.exit(1);
  }
  