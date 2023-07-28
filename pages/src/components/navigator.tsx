export function Navigator() {
  const pages = [
    {
      descr: "Recent Builds",
      link: "/",
    },
    {
      descr: "Github",
      link: "https://github.com/SynthstromAudible/DelugeFirmware/",
    },
  ];

  const pageItems = pages.map((page, i) => (
    <li className="flex-none" key={i}>
      <a href={page.link} className="underline">
        {page.descr}
      </a>
    </li>
  ));

  return (
    <div className="md:h-20">
      <div
        className="
        bg-neutral-900 w-full
        md:absolute md:top-0 md:right-0 md:h-20
        flex items-baseline
        flex-col md:flex-row
      "
      >
        <div
          className="
          flex-none p-4
          text-4xl font-bold
          border-b-2 border-neutral-50
          md:border-b-0
          w-full
          md:w-96
        "
        >
          Deluge Firmware
        </div>
        <nav className="w-full p-4 text-2xl">
          <ul
            className="
            flex gap-4 pr-8
            justify-center flex-col
            md:justify-end md:flex-row
          "
          >
            {pageItems}
          </ul>
        </nav>
      </div>
    </div>
  );
}
