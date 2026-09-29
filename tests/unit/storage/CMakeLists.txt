# tests/unit/storage/CMakeLists.txt
#
# deluge::storage specs: the header-only seams under src/deluge/storage/.
deluge_specs_driver(storage_specs
  INCLUDES
    ../../../src/deluge # storage/*.h
)
