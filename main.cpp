#include <intrinsics.hpp>


bool NAKED is_zero_page(PVOID page)
{
    __asm
    {
        vpxor ymm0, ymm0, ymm0
        mov eax, 4096 / 32
        loop:
        vmovdqu ymm1, [rcx]
            vpxor ymm0, ymm0, ymm1
            add rcx, 32
            vptest ymm0, ymm0
            jnz nonzero

            dec eax
            jnz loop

            mov al, 1
            vzeroupper
            ret

            nonzero :
        xor eax, eax
            vzeroupper
            ret
    }
}

void FilterRawRanges(PHYSICAL_MEMORY_RANGE* range_0, PHYSICAL_MEMORY_RANGE* range_1)
{
    auto rva = MmMapIoSpace(range_0->BaseAddress.QuadPart, range_0->NumberOfBytes.QuadPart, MmNonCached);
    if (rva)
    {
        int largest_zero_range = 0;
        int current_zero_range = 0;
        UINT64 largest_zero_range_start = 0;
        for (UINT64 current = (UINT64)rva; current < (UINT64)rva + range_0->NumberOfBytes.QuadPart; current += 4096)
        {
            if (is_zero_page((PVOID)current))
            {
                current_zero_range++;
            }
            else
            {
                if (current_zero_range > largest_zero_range)
                {
                    largest_zero_range = current_zero_range;
                    largest_zero_range_start = (current - (current_zero_range * 4096)) - (UINT64)rva + range_0->BaseAddress.QuadPart;
                }
                current_zero_range = 0;
            }
        }

        MmUnmapIoSpace((PVOID)rva, range_0->NumberOfBytes.QuadPart);

		range_0->BaseAddress.QuadPart = largest_zero_range_start;
		range_0->NumberOfBytes.QuadPart = largest_zero_range * 4096;
    }


    rva = MmMapIoSpace(range_1->BaseAddress.QuadPart, range_1->NumberOfBytes.QuadPart, MmNonCached);
    if (rva)
    {
        int largest_zero_range = 0;
        int current_zero_range = 0;
        UINT64 largest_zero_range_start = 0;
        for (UINT64 current = (UINT64)rva; current < (UINT64)rva + range_1->NumberOfBytes.QuadPart; current += 4096)
        {
            if (is_zero_page((PVOID)current))
            {
                current_zero_range++;
            }
            else
            {
                if (current_zero_range > largest_zero_range)
                {
                    largest_zero_range = current_zero_range;
                    largest_zero_range_start = (current - (current_zero_range * 4096)) - (UINT64)rva + range_1->BaseAddress.QuadPart;
                }
                current_zero_range = 0;
            }
        }

        MmUnmapIoSpace((PVOID)rva, range_1->NumberOfBytes.QuadPart);

		range_1->BaseAddress.QuadPart = largest_zero_range_start;
		range_1->NumberOfBytes.QuadPart = largest_zero_range * 4096;
    }
    return;
}

bool ReclaimFirmwareMemory(PHYSICAL_MEMORY_RANGE* safe_data, PHYSICAL_MEMORY_RANGE* safe_executable)
{
    safe_data->NumberOfBytes.QuadPart = 0;
    PHYSICAL_MEMORY_RANGE* pmr = MmGetPhysicalMemoryRanges();
    pmr += 2;
    int module_count = 0;
    do
    {
        if (&pmr[0] && !pmr[0].NumberOfBytes.QuadPart)
            break;
        if (&pmr[1] && !pmr[1].NumberOfBytes.QuadPart)
            break;

        auto high = pmr[1].BaseAddress.QuadPart - 0x1000;
        auto low = pmr[0].BaseAddress.QuadPart + pmr[0].NumberOfBytes.QuadPart + 0x1000;
        auto size = high - low;

        if (size > 0x40000000)
            size = 0x40000000;

		safe_data->BaseAddress.QuadPart = low;
        auto rva = (UINT64)MmMapIoSpace(low, size, MmNonCached);
        if (rva)
        {
            for (UINT64 current = rva; current < rva + size; current += 4096)
            {
                UINT64 hdr_offset = *(BYTE*)(current + 0x3C);
                if (*(UINT16*)current == 0x5A4D &&                  // 'MZ' Hdr
                    *(UINT16*)(current + hdr_offset + 4) == 0x8664) // AMD64
                {
                    auto sizeofimage = *(UINT32*)(current + hdr_offset + 0x50);
                    if (sizeofimage & 0xFFF)
                        sizeofimage = (sizeofimage & ~0xFFF) + 0x1000;

                    if (module_count == 0)
                        safe_data->NumberOfBytes.QuadPart = (low + (current - rva)) - safe_data->BaseAddress.QuadPart;

                    if (module_count == 0)
                    {
                        safe_executable->BaseAddress.QuadPart = low + (current - rva) + sizeofimage;
                    }
                    else if (module_count == 1)
                    {
                        safe_executable->NumberOfBytes.QuadPart = (low + (current - rva)) - safe_executable->BaseAddress.QuadPart;
                        MmUnmapIoSpace((PVOID)rva, size);

						FilterRawRanges(safe_data, safe_executable);
                        return true;
                    }
                    module_count++;
                }
            }
            MmUnmapIoSpace((PVOID)rva, size);
        }
        pmr++;
    } while (true);
    return false;
}



NTSTATUS DriverEntry()
{
    PHYSICAL_MEMORY_RANGE safe_range[2]{0};
    if (ReclaimFirmwareMemory(&safe_range[0], &safe_range[1]))
    {
		printf("range 0: %p-%p size=%x\n", safe_range[0].BaseAddress.QuadPart, safe_range[0].BaseAddress.QuadPart - safe_range[0].NumberOfBytes.QuadPart, safe_range[0].NumberOfBytes.QuadPart);
		printf("range 1: %p-%p size=%x\n", safe_range[1].BaseAddress.QuadPart, safe_range[1].BaseAddress.QuadPart - safe_range[1].NumberOfBytes.QuadPart, safe_range[1].NumberOfBytes.QuadPart);
    }
    return STATUS_SUCCESS;
}