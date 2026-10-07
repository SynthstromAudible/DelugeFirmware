//! The version the upgrader stamps its output with is the one the firmware stamps its own saves with:
//! `c` and the project version of the repository's CMakeLists.txt (src/deluge/version/CMakeLists.txt).

fn main() {
    let cmake = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../CMakeLists.txt");
    println!("cargo:rerun-if-changed={}", cmake.display());
    let text = std::fs::read_to_string(&cmake).expect("the repository's CMakeLists.txt");
    let project = text
        .find("project(Deluge")
        .expect("a project(Deluge ...) block");
    let version = text[project..]
        .split_whitespace()
        .skip_while(|word| *word != "VERSION")
        .nth(1)
        .expect("a VERSION in project(Deluge ...)");
    println!("cargo:rustc-env=DELUGE_FIRMWARE_VERSION=c{version}");
}
