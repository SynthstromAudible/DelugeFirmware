# Usage from GDB:
#   source ./contrib/debug/heap_state.py
#   python
#
import itertools

try:
    # We attempt to import GDB, and if it fails fall back to an emulation mode
    import gdb
    import re

    _SPACE_MATCHER = re.compile(r"0x([0-9a-fA-F]+) - 0x([0-9a-fA-F]+) is \.(.+)$")
    HEAP_START = None
    HEAP_END = None

    for line in gdb.execute("info files", to_string=True).split("\n"):
        m = _SPACE_MATCHER.search(line)
        if m is not None:
            (start, end, name) = m.groups()
            if name == "data":
                HEAP_START = int(end, 16)
            elif name == "program_stack":
                HEAP_END = int(start, 16)

    arch = gdb.selected_inferior().architecture()
    TY_INT32 = arch.integer_type(32, True)
    TY_UINT32 = arch.integer_type(32, False)
    TY_BOOL = gdb.lookup_type("bool")
    TY_BidirectionalLinkedListNode = gdb.lookup_type("BidirectionalLinkedListNode")
    TY_BidirectionalLinkedList = gdb.lookup_type("BidirectionalLinkedList")
    TY_CacheManager = gdb.lookup_type("CacheManager")
    TY_ResizeableArray = gdb.lookup_type("ResizeableArray")
    TY_OrderedResizeableArray = gdb.lookup_type("OrderedResizeableArray")
    TY_OrderedResizeableArrayWith32bitKey = gdb.lookup_type(
        "OrderedResizeableArrayWith32bitKey"
    )
    TY_OrderedResizeableArrayWithMultiWordKey = gdb.lookup_type(
        "OrderedResizeableArrayWithMultiWordKey"
    )
    TY_EmptySpaceRecord = gdb.lookup_type("EmptySpaceRecord")
    TY_MemoryRegion = gdb.lookup_type("MemoryRegion")
    TY_GeneralMemoryAllocator = gdb.lookup_type("GeneralMemoryAllocator")

    # depending on optimization the GMA singleton can be a global function
    # or inlined variable, in which case it will not get found by lookup
    try:
        GMA_START_OFFSET = gdb.lookup_global_symbol(
            "GeneralMemoryAllocator::get()::generalMemoryAllocator"
        ).address.cast(TY_UINT32)
    except:
        print("falling back to string parsing")
        GMA_START_OFFSET = int(
            gdb.execute(
                "info var ^GeneralMemoryAllocator::get()::generalMemoryAllocator$",
                to_string=True,
            )
            .split("\n")[3]
            .split()[0],
            16,
        )

except ModuleNotFoundError as e:
    gdb = None

    class Field:
        """Emulates a gdb.Field"""

        def __init__(
            self,
            name,
            bitpos=None,
            enumval=None,
            artificial=False,
            is_base_class=True,
            bitsize=0,
            ty=None,
        ):
            self.name = name
            self.bitpos = bitpos
            self.bitsize = bitsize
            if enumval is not None:
                self.enumval = enumval

    class Type:
        """Very low effort emulation of the GDB Type API

        https://sourceware.org/gdb/onlinedocs/gdb/Types-In-Python.html#Types-In-Python
        """

        def __init__(self, name, alignof, sizeof, fields, dynamic=False):
            self.alignof = alignof
            self.sizeof = sizeof
            self.dynamic = dynamic
            self.name = name
            self.code = 0
            self.fields_ = fields

        def fields(self):
            return self.fields_

    class FakeInferior:
        """Fakes the GDB Inferior class

        https://sourceware.org/gdb/onlinedocs/gdb/Inferiors-In-Python.html#Inferiors-In-Python
        """

        def __init__(self, memory, base_offset):
            self.memory = memory
            self.base_offset = base_offset
            self.max_offset = base_offset + len(memory)

        def read_memory(self, offset, length):
            if offset < self.base_offset:
                raise ValueError(f"offset {offset:x} too low")
            if offset + length > self.max_offset:
                raise ValueError(f"offset+len too high {offset+length:x}")

            offset -= self.base_offset
            return self.memory[offset : offset + length]

    TY_INT32 = Type("int32_t", 4, 4, [])
    TY_UINT32 = Type("uint32_t", 4, 4, [])
    TY_PTR = Type(None, 4, 4, [])
    TY_BOOL = Type("bool", 1, 1, [])
    TY_BidirectionalLinkedListNode = Type(
        "BidirectionalLinkedListNode",
        4,
        16,
        [
            Field("_vptr.xxx", bitpos=0, ty=TY_PTR),
            Field("next", bitpos=32, ty=TY_PTR),
            Field("prevPointer", bitpos=64, ty=TY_PTR),
            Field("list", bitpos=96, ty=TY_PTR),
        ],
    )
    TY_BidirectionalLinkedList = Type(
        "BidirectionalLinkedList",
        4,
        20,
        [
            Field("endNode", bitpos=0, ty=TY_BidirectionalLinkedListNode),
            Field("first", bitpos=128, ty=TY_PTR),
        ],
    )
    TY_CacheManager = Type(
        "CacheManager",
        4,
        240,
        [
            Field(
                "reclamation_queue_",
                bitpos=0,
                ty="std::array<BidirectionalLinkedList, 10>",
            ),
            Field(
                "longest_runs_", bitpos=1600, ty="struct std::array<unsigned long, 10>"
            ),
        ],
    )
    TY_ResizeableArray = Type(
        "ResizeableArray",
        4,
        44,
        [
            Field("elementSize", bitpos=0, ty=TY_UINT32),
            Field("emptyingShouldFreeMemory", bitpos=32, ty=TY_BOOL),
            Field("staticMemoryAllocationSize", bitpos=64, ty=TY_UINT32),
            Field("memory", bitpos=96, ty=TY_PTR),
            Field("numElements", bitpos=128, ty=TY_INT32),
            Field("memorySize", bitpos=160, ty=TY_INT32),
            Field("memoryStart", bitpos=192, ty=TY_INT32),
            Field("lock", bitpos=224, ty=TY_BOOL),
            Field("memoryAllocationStart", bitpos=256, ty=TY_PTR),
            Field("maxNumEmptySpacesToKeep", bitpos=288, ty=TY_INT32),
            Field("numExtraSpacesToAllocate", bitpos=320, ty=TY_INT32),
        ],
    )
    TY_OrderedResizeableArray = Type(
        "OrderedResizeableArray",
        4,
        56,
        [
            Field("ResizeableArray", bitpos=0, ty=TY_ResizeableArray),
            Field("keyMask", bitpos=352, ty=TY_UINT32),
            Field("keyOffset", bitpos=384, ty=TY_UINT32),
            Field("keyShiftAmount", bitpos=416, ty=TY_INT32),
        ],
    )
    TY_OrderedResizeableArrayWith32bitKey = Type(
        "OrderedResizeableArrayWith32bitKey",
        4,
        56,
        [Field("OrderedResizeableArray", bitpos=0, ty=TY_OrderedResizeableArray)],
    )
    TY_OrderedResizeableArrayWithMultiWordKey = Type(
        "OrderedResizeableArrayWithMultiWordKey",
        4,
        60,
        [
            Field(
                "OrderedResizeableArrayWith32bitKey",
                bitpos=0,
                ty=TY_OrderedResizeableArrayWith32bitKey,
            ),
            Field("numWordsInKey", bitpos=448, ty=TY_INT32),
        ],
    )
    TY_EmptySpaceRecord = Type(
        "EmptySpaceRecord",
        4,
        8,
        [
            Field("length", bitpos=0, ty=TY_UINT32),
            Field("address", bitpos=32, ty=TY_UINT32),
        ],
    )
    TY_MemoryRegion = Type(
        "MemoryRegion",
        4,
        308,
        [
            Field(
                "emptySpaces", bitpos=0, ty=TY_OrderedResizeableArrayWithMultiWordKey
            ),
            Field("numAllocations", bitpos=480, ty=TY_INT32),
            Field("name", bitpos=512, ty="char const *"),
            Field("cache_manager_", bitpos=544, ty=TY_CacheManager),
        ],
    )
    TY_GeneralMemoryAllocator = Type(
        "GeneralMemoryAllocator",
        4,
        620,
        [
            Field("regions", bitpos=0, ty="class MemoryRegion [2]"),
            Field("lock", bitpos=4928, ty=TY_BOOL),
        ],
    )

    HEAP_START = None
    HEAP_END = None
    GMA_START_OFFSET = 0x2003001C


def bytes_as_uint32(b):
    assert len(b) == 4
    if isinstance(b, memoryview):
        b = b.tobytes()
    return (b[3] << 24) | (b[2] << 16) | (b[1] << 8) | b[0]


def iter_chunks(i, n):
    while chunk := list(itertools.islice(i, n)):
        yield chunk


def print_bytes(base_offset, data):
    N = 16
    data_iter = iter(data)
    offset = base_offset & ~0xF
    next_line = offset + N
    print(f"{offset:08x}", end=" ")
    while offset < base_offset:
        offset += 1
        print(end="   ")
    while offset < next_line:
        offset += 1
        print("{:02x}".format(next(data_iter)), end=" ")
    print()
    for i, chunk in enumerate(iter_chunks(data_iter, N)):
        addr = offset + i * N
        print(f"{addr:08x}", end=" ")
        for d in chunk:
            print(f"{d:02x}", end=" ")
        print()


def find_field(ty, name):
    for field in ty.fields():
        if field.name == name:
            return field
    return None


def parse_empty_region(inferior, offset):
    memory = inferior.read_memory(offset, TY_EmptySpaceRecord.sizeof)
    length = bytes_as_uint32(memory[0:4])
    address = bytes_as_uint32(memory[4:8])
    print(f"EmptyRegion{{ addr:{address:08x}, len:{length:08x} }}")


def walk_linked_list_from_node(inferior, offset, end):
    next_offset = find_field(TY_BidirectionalLinkedListNode, "next").bitpos // 8
    list_offset = find_field(TY_BidirectionalLinkedListNode, "list").bitpos // 8

    while offset != end:
        try:
            memory = inferior.read_memory(offset, TY_BidirectionalLinkedListNode.sizeof)
            offset = bytes_as_uint32(inferior.read_memory(offset + next_offset, 4))
            list_addr = bytes_as_uint32(inferior.read_memory(offset + list_offset, 4))
            print(f"Stealable node at: {offset:08x} for list {list_addr:08x}")
            # print_bytes(offset, memory)
        except ValueError:
            print("WARNING: possibly corrupt list, exiting earlier")
            return


def parse_cache_manager(inferior, offset):
    memory = inferior.read_memory(offset, TY_CacheManager.sizeof)
    # 10 is NUM_STEALABLE_QUEUES
    for i in range(10):
        print("---- Stealables for stealable queue", i)
        ll_offset = offset + TY_BidirectionalLinkedList.sizeof * i
        first_offset = find_field(TY_BidirectionalLinkedList, "first").bitpos // 8
        end_offset = find_field(TY_BidirectionalLinkedList, "endNode").bitpos // 8

        first = bytes_as_uint32(inferior.read_memory(ll_offset + first_offset, 4))
        walk_linked_list_from_node(inferior, first, ll_offset + end_offset)


def parse_memory_region(inferior, offset, mem_region_idx):
    empty_offset = find_field(TY_MemoryRegion, "emptySpaces").bitpos // 8 + offset
    cache_manager_offset = (
        find_field(TY_MemoryRegion, "cache_manager_").bitpos // 8 + offset
    )

    num_elements_offset = find_field(TY_ResizeableArray, "numElements").bitpos // 8
    memory_offset = find_field(TY_ResizeableArray, "memory").bitpos // 8
    memory_start_offset = find_field(TY_ResizeableArray, "memoryStart").bitpos // 8
    element_size_offset = find_field(TY_ResizeableArray, "elementSize").bitpos // 8
    words_in_key_offset = (
        find_field(TY_OrderedResizeableArrayWithMultiWordKey, "numWordsInKey").bitpos
        // 8
    )

    memory_val = bytes_as_uint32(inferior.read_memory(empty_offset + memory_offset, 4))
    memory_start = bytes_as_uint32(
        inferior.read_memory(empty_offset + memory_start_offset, 4)
    )
    num_elements = bytes_as_uint32(
        inferior.read_memory(empty_offset + num_elements_offset, 4)
    )
    element_size = bytes_as_uint32(
        inferior.read_memory(empty_offset + element_size_offset, 4)
    )
    words_in_key = bytes_as_uint32(
        inferior.read_memory(empty_offset + words_in_key_offset, 4)
    )
    memory = inferior.read_memory(
        empty_offset, TY_OrderedResizeableArrayWithMultiWordKey.sizeof
    )

    MEM_REGION_NAMES = [
        "STEALABLE",
        "INTERNAL",
        "EXTERNAL",
    ]
    mem_region_name = MEM_REGION_NAMES[mem_region_idx]

    print(
        f"== Memory Region {mem_region_name}: empty region list: {memory_val:08x}, count={num_elements}"
    )
    if words_in_key != 2:
        print("Unexpected words_in_key", words_in_key, "exiting early")
        return

    print("--- Empty regions:")
    for i in range(num_elements):
        parse_empty_region(
            inferior, memory_val + (i + memory_start) * TY_EmptySpaceRecord.sizeof
        )

    print("--- Stealable regions:")
    parse_cache_manager(inferior, cache_manager_offset)


def parse_heap(inferior, start, end):
    SPACE_HEADER_EMPTY = 0
    SPACE_HEADER_STEALABLE = 0x40000000
    SPACE_HEADER_ALLOCATED = 0x80000000
    SPACE_HEADER_BITS = SPACE_HEADER_STEALABLE | SPACE_HEADER_ALLOCATED

    block_start = start
    while block_start < end:
        block_header = bytes_as_uint32(inferior.read_memory(block_start, 4))
        length = block_header & ~SPACE_HEADER_BITS
        if length == 0:
            print(f"{block_start:08x} - {block_start+4:08x} Zero-length block")
            block_start += 4
        else:
            block_footer = bytes_as_uint32(
                inferior.read_memory(block_start + length + 4, 4)
            )
            footer_length = block_footer & ~SPACE_HEADER_BITS
            if footer_length != length:
                print(
                    f"{block_start:08x} - Possibly corrupt block {block_header:08x} {block_footer:08x}"
                )
            else:
                block_mode = block_header & SPACE_HEADER_BITS
                mode_string = ""
                if block_mode == SPACE_HEADER_EMPTY:
                    mode_string = "EMPTY"
                elif block_mode == SPACE_HEADER_STEALABLE:
                    mode_string = "STEALABLE"
                elif block_mode == SPACE_HEADER_ALLOCATED:
                    mode_string = "ALLOCATED"
                else:
                    mode_string = "STEALABLE|ALLOCATED"
                print(
                    f"{block_start+4:08x} - {block_start+length+4:08x} {length:8x} {mode_string}"
                )

            block_start += length + 8


def parse_gma(inferior=gdb.inferiors()[0]):
    # iterate over both memory regions
    regions_offset = find_field(TY_GeneralMemoryAllocator, "regions").bitpos // 8
    for i in range(3):
        parse_memory_region(
            inferior, GMA_START_OFFSET + regions_offset + TY_MemoryRegion.sizeof * i, i
        )

    if HEAP_START is not None:
        EXTERNAL_MEMORY_START = 0x0C000000
        EXTERNAL_MEMORY_END = 0x10000000
        RESERVED_EXTERNAL_ALLOCATOR = 0x00100000

        print("** Stealable heap blocks:")
        parse_heap(
            inferior,
            EXTERNAL_MEMORY_START,
            EXTERNAL_MEMORY_END - RESERVED_EXTERNAL_ALLOCATOR,
        )
        print("** External heap blocks:")
        parse_heap(
            inferior,
            EXTERNAL_MEMORY_END - RESERVED_EXTERNAL_ALLOCATOR,
            EXTERNAL_MEMORY_END,
        )
        print("** Internal Heap blocks:")
        parse_heap(inferior, HEAP_START, HEAP_END)
    else:
        print("WARNING: no HEAP_START provided, not parsing")


def parse_args():
    import argparse

    parser = argparse.ArgumentParser()
    parser.add_argument("memory_dump")
    parser.add_argument("memory_offset")

    return parser.parse_args()


def main():
    import sys

    parser = parse_args()
    with open(parser.memory_dump, "rb") as inf:
        inf = FakeInferior(inf.read(), 0x20020000)

    parse_gma(inf)


if __name__ == "__main__" and gdb is None:
    main()
