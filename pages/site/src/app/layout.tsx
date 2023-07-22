import "./globals.css";
import type { Metadata } from "next";
import { Karla } from "next/font/google";
import { Navigator } from "../components/navigator";

const karla = Karla({ subsets: ["latin"] });

export const metadata: Metadata = {
  title: "Deluge Firmware",
  description: "Site for the Synthstrom Audible Deluge's firmware",
};

export default function RootLayout({
  children,
}: {
  children: React.ReactNode;
}) {
  const bodyClasses = `${karla.className} p-0 m-0 bg-neutral-900 text-neutral-50`;
  return (
    <html lang="en">
      <body className={bodyClasses}>
        <div className="flex flex-col">
          <Navigator></Navigator>
          {children}
        </div>
      </body>
    </html>
  );
}
