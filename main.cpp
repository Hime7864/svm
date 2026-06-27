#include <intrinsics.hpp>


bool ReclaimFirmwareMemory(PHYSICAL_MEMORY_RANGE* range_0, PHYSICAL_MEMORY_RANGE* range_1)
{
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
                    {
                        range_0->BaseAddress.QuadPart = low + (current - rva) + sizeofimage;
                    }
                    else if (module_count == 1)
                    {
                        range_0->NumberOfBytes.QuadPart = (low + (current - rva)) - range_0->BaseAddress.QuadPart;
                        range_1->BaseAddress.QuadPart = low + (current - rva) + sizeofimage;
                    }
                    else if (module_count == 2)
                    {
                        range_1->NumberOfBytes.QuadPart = (low + (current - rva)) - range_1->BaseAddress.QuadPart;
                        MmUnmapIoSpace((PVOID)rva, size);
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

NTSTATUS DriverEntry()
{

	PHYSICAL_MEMORY_RANGE safe_range[2];
    ReclaimFirmwareMemory(&safe_range[0], &safe_range[1]);

	auto rva = MmMapIoSpace(safe_range[0].BaseAddress.QuadPart, safe_range[0].NumberOfBytes.QuadPart, MmNonCached);

    int largest_zero_range = 0;
	int current_zero_range = 0;
	UINT64 largest_zero_range_start = 0;
    for(UINT64 current = (UINT64)rva; current < (UINT64)rva + safe_range[0].NumberOfBytes.QuadPart; current += 4096)
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
                largest_zero_range_start = (current - (current_zero_range * 4096)) - (UINT64)rva + safe_range[0].BaseAddress.QuadPart;
			}
            current_zero_range = 0;
		}
    }
    MmUnmapIoSpace((PVOID)rva, safe_range[0].NumberOfBytes.QuadPart);

    rva = MmMapIoSpace(safe_range[1].BaseAddress.QuadPart, safe_range[1].NumberOfBytes.QuadPart, MmNonCached);
	printf("range 0: %p %d pages\n", largest_zero_range_start, largest_zero_range);

    largest_zero_range = 0;
    current_zero_range = 0;
    largest_zero_range_start = 0;
    for (UINT64 current = (UINT64)rva; current < (UINT64)rva + safe_range[1].NumberOfBytes.QuadPart; current += 4096)
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
                largest_zero_range_start = (current - (current_zero_range * 4096)) - (UINT64)rva + safe_range[1].BaseAddress.QuadPart;
            }
            current_zero_range = 0;
        }
    }

    printf("range 1: %p %d pages\n", largest_zero_range_start, largest_zero_range);

	MmUnmapIoSpace((PVOID)rva, safe_range[1].NumberOfBytes.QuadPart);

    return STATUS_SUCCESS;
}