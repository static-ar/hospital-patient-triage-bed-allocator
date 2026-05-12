# how best-fit worked in our hospital bed allocator

for our operating systems lab project, we built a hospital patient triage and bed allocator in c on linux. the hospital ward was modeled as a contiguous memory block. each patient required a number of care units depending on severity: critical patients required 3 units, isolation patients required 2 units, and general patients required 1 unit.

we implemented three allocation strategies: first-fit, best-fit, and worst-fit. first-fit scans from the start and chooses the first partition large enough. worst-fit chooses the largest free partition. best-fit chooses the smallest free partition that can still satisfy the patient requirement.

best-fit usually reduced wasted space because it avoided using a very large free block for a small patient. for example, if a patient needed 1 care unit and free blocks of sizes 1, 2, and 3 were available, best-fit selected the size 1 block. this kept larger blocks available for icu or isolation patients.

however, best-fit was not perfect. after many allocations and discharges, it sometimes created small scattered free spaces. this increased external fragmentation. to handle this, we implemented coalescing. when a patient was discharged, the freed partition was merged with adjacent free partitions on the left and right side.

we logged fragmentation after each allocation and deallocation using this formula:

`(1 - largest_free_block / total_free_units) * 100`

this helped us compare how the memory layout changed during the simulation. the project made memory management concepts much easier to understand because bed allocation behaved like real partition allocation in an operating system.


authors: Muhammad Talha (23F-0511), Abdul Rafay (23F-0591), Masooma Mirza (23F-0876)
