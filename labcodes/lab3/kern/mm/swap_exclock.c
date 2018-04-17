#include <defs.h>
#include <x86.h>
#include <stdio.h>
#include <string.h>
#include <swap.h>
#include <swap_exclock.h>
#include <list.h>

list_entry_t pra_list_head;
list_entry_t *clock_head;

static int
_exclock_init_mm(struct mm_struct *mm)
{
     list_init(&pra_list_head);
     mm->sm_priv = &pra_list_head;
     clock_head = &pra_list_head;
     //cprintf(" mm->sm_priv %x in exclock_init_mm\n",mm->sm_priv);
     return 0;
}
/*
 * (3)_exclock_map_swappable: According exclock PRA, we should link the most recent arrival page at the back of pra_list_head qeueue
 */
static int
_exclock_map_swappable(struct mm_struct *mm, uintptr_t addr, struct Page *page, int swap_in)
{
    list_entry_t *head = clock_head;
    list_entry_t *entry=&(page->pra_page_link);

    assert(entry != NULL && head != NULL);
    //record the page access situlation
    /*LAB3 EXERCISE 2: YOUR CODE*/
    //(1)link the most recent arrival page at the back of the pra_list_head qeueue.
    list_add_before(head, entry);
    if (clock_head == mm->sm_priv) clock_head = entry;
    return 0;
}
/*
 *  (4)_exclock_swap_out_victim: According exclock PRA, we should unlink the  earliest arrival page in front of pra_list_head qeueue,
 *                            then assign the value of *ptr_page to the addr of this page.
 */
static int
_exclock_swap_out_victim(struct mm_struct *mm, struct Page ** ptr_page, int in_tick)
{
    assert(clock_head != NULL);
    assert(in_tick==0);
    /* Select the victim */
    assert(clock_head != mm->sm_priv);
    while (1) {
        if (clock_head == mm->sm_priv) clock_head = list_next(clock_head);
        struct Page* page = le2page(clock_head, pra_page_link);
        uintptr_t va = page->pra_vaddr;
        pte_t *ptep = get_pte(mm->pgdir, va, 0);
        assert((*ptep & PTE_P) != 0);
        if ( !(*ptep & PTE_A) && !(*ptep & PTE_D) ) {
            list_entry_t *next = list_next(clock_head);
            if (next == mm->sm_priv && list_prev(clock_head) != mm->sm_priv) next = list_next(next);
            list_del(clock_head);
            clock_head = next;
            assert(page != NULL);
            *ptr_page = page;
            return 0;
        }
        else if ( !(*ptep & PTE_A) && (*ptep & PTE_D) ) {
            *ptep &= ~PTE_D;
            if (swapfs_write( (va/PGSIZE+1)<<8, page) != 0) {
                cprintf("CLOCK_EXTENDED WRITE: failed to save\n");
            } else {
                cprintf("CLOCK_EXTENDED WRITE: store page in vaddr 0x%x to disk swap entry %d\n", va, va/PGSIZE+1);
            }
            tlb_invalidate(mm->pgdir, va);
        } else if ( (*ptep & PTE_A) ) {
            *ptep &= ~PTE_A;
            tlb_invalidate(mm->pgdir, va);
        }
        clock_head = list_next(clock_head);
    }
}

static int
_exclock_check_swap(void) {
    // page_A_D, eg:e11 means Page e, Acessed 1, Dirty 1.
    cprintf("write Virt Page c in exclock_check_swap\n");
    *(unsigned char *)0x3000 = 0x0c;
    assert(pgfault_num==4);
    cprintf("write Virt Page a in exclock_check_swap\n");
    *(unsigned char *)0x1000 = 0x0a;
    assert(pgfault_num==4);
    cprintf("write Virt Page d in exclock_check_swap\n");
    *(unsigned char *)0x4000 = 0x0d;
    assert(pgfault_num==4);
    cprintf("write Virt Page b in exclock_check_swap\n");
    *(unsigned char *)0x2000 = 0x0b;
    assert(pgfault_num==4);
    cprintf("write Virt Page e in exclock_check_swap\n");
    *(unsigned char *)0x5000 = 0x0e;
    assert(pgfault_num==5);    // e11^b00c00d00
    cprintf("read Virt Page b in exclock_check_swap\n");
    assert(*(unsigned char *)0x2000 == 0x0b);
    assert(pgfault_num==5);    // e11^b10c00d00
    cprintf("write Virt Page a in exclock_check_swap\n");
    *(unsigned char *)0x1000 = 0x0a;
    assert(pgfault_num==6);    // e11b00a11^d00
    cprintf("read Virt Page b in exclock_check_swap\n");
    assert(*(unsigned char *)0x2000 == 0x0b);
    assert(pgfault_num==6);    // e11b10a11^d00
    cprintf("write Virt Page c in exclock_check_swap\n");
    *(unsigned char *)0x3000 = 0x0c;
    assert(pgfault_num==7);    // ^e11b10a11c11
    cprintf("write Virt Page d in exclock_check_swap\n");
    *(unsigned char *)0x4000 = 0x0d;
    assert(pgfault_num==8);    // e*00d11^a01c01
    cprintf("read Virt Page e in exclock_check_swap\n");
    assert(*(unsigned char *)0x5000 == 0x0e);
    assert(pgfault_num==8);    // e*10d11^a01c01
    cprintf("write Virt Page b in exclock_check_swap\n");
    *(unsigned char *)0x2000 = 0x0b;
    assert(pgfault_num==9);   // e*00d01b11^c*00
    cprintf("write Virt Page a in exclock_check_swap\n");
    *(unsigned char *)0x1000 = 0x0a;
    assert(pgfault_num==10);
    return 0;
}


static int
_exclock_init(void)
{
    return 0;
}

static int
_exclock_set_unswappable(struct mm_struct *mm, uintptr_t addr)
{
    return 0;
}

static int
_exclock_tick_event(struct mm_struct *mm)
{ return 0; }


struct swap_manager swap_manager_exclock =
{
     .name            = "exclock swap manager",
     .init            = &_exclock_init,
     .init_mm         = &_exclock_init_mm,
     .tick_event      = &_exclock_tick_event,
     .map_swappable   = &_exclock_map_swappable,
     .set_unswappable = &_exclock_set_unswappable,
     .swap_out_victim = &_exclock_swap_out_victim,
     .check_swap      = &_exclock_check_swap,
};
