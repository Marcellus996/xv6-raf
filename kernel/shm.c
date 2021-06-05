#include "param.h"
#include "types.h"
#include "defs.h"
#include "spinlock.h"
#include "mmu.h"
#include "memlayout.h"
#include "proc.h"
#include "fcntl.h"

#define SHMNAME    16  // maximum size for name of shared memory object

struct shm_obj {
  char name[SHMNAME];
  int size; 
  int pgc;
  char* pgs[SHMPGS];
  int proc_cnt;  // number of processes using this object
};

struct {
    struct spinlock lock;
    struct shm_obj shm_objs[SHMOBJ];
} shm;

void
clear_shm_obj(int obj)
{
    int i;

    for (i = 0; i < SHMNAME; i++)
        shm.shm_objs[obj].name[i] = '\0';
    for (i = 0; i < SHMPGS; i++)
        shm.shm_objs[obj].pgs[i] = 0;
    shm.shm_objs[obj].pgc = 0;
    shm.shm_objs[obj].size = 0;
    shm.shm_objs[obj].proc_cnt = 0;
}

void shminit(void)
{
    int i;

    for (i = 0; i < SHMOBJ; i++)
    {
        clear_shm_obj(i);
    }

    initlock(&shm.lock, "shm");
}

void
unallocate_shm_obj(int obj)
{
    int i;
    
    for (i = 0; i < shm.shm_objs[obj].pgc; i++)
    {
        kfree(shm.shm_objs[obj].pgs[i]);
    }
}

int
name_cmp(struct shm_obj* p, char* name)
{
    int i, s;

    s = strlen(name);
    for (i = 0; i < SHMNAME; i++)
    {
        if (i < s) {
            if (p->name[i] != name[i])
                return 0;
        } else {
            if (p->name[i] != '\0') {
                return 0;
            }
        }
    }
        
    return 1;
}

void
name_set(struct shm_obj* p, char* name)
{
    int i, s;

    s = strlen(name);
    for (i = 0; i < SHMNAME; i++)
    {
        if (i < s) {
            p->name[i] = name[i];
        } else {
            p->name[i] = '\0';
        }
    }
}

// returns -1 if not found
int find_with_name(char *name)
{
    struct shm_obj* pt_shm_obj;
    int i;
    
    for (pt_shm_obj = shm.shm_objs, i = 0; pt_shm_obj < &shm.shm_objs[SHMOBJ]; pt_shm_obj++, i++) {
        if (name_cmp(pt_shm_obj, name) == 1) {
            return i;
        }
    }

    return -1;
}

// returns -1 if no empty
int find_empty()
{
    struct shm_obj* pt_shm_obj;
    int i;
    
    for (pt_shm_obj = shm.shm_objs, i = 0; pt_shm_obj < &shm.shm_objs[SHMOBJ]; pt_shm_obj++, i++) {
        if (pt_shm_obj->name[0] == '\0') {
            return i;
        }
    }

    return -1;
}

// Ovaj sistemski poziv kreira objekat sa navednim imenom ako on ne postoji, a u suprotnom
// ga samo otvara za trenutni proces. U svakom slučaju se u proc strukturi trenutnog procesa
// zapisuje da je ovaj objekat sada otvoren za trenutni proces, i vraća se fd koji opisuje taj
// objekat. Povratne vrednosti za ovaj sistemski poziv su:
// ● -1: objekat nije uspešno kreiran
// ● >0: uspešno otvoren objekat, i vraćena vrednost je fd koji opisuje objekat.
int
shm_open(char *name)
{
    struct proc* p;
    int i;

    acquire(&shm.lock);
    i = find_with_name(name);
    if (i == -1)
    {
        // Not allocated
        i = find_empty();
        if (i == -1)
        {
            // No empty slots
            release(&shm.lock);
            return -1;
        }

        // Newly allocated, set name
        name_set(&shm.shm_objs[i], name);
    }
    shm.shm_objs[i].proc_cnt++;
    release(&shm.lock);
    
    p = myproc();
    // Shared memory open for process
    p->shm[i] = 1;

    // We use index as fd, it's easier
    return i;
}

// int shm_trunc(int fd, int size)
// Ovaj sistemski poziv podešava veličinu novokreiranog shm objekta na size. Sistemski poziv
// treba da ima efekta samo ako je u pitanju prvi shm_trunc() poziv za navedeni objekat.
// Povratne vrednosti za ovaj sistemski poziv su:
// ● -1: neuspešna alokacija za objekat.
// ● >0: podešavanje veličine shm objekta je završeno uspešno, i vraćena vrednost je
// veličina objekta.
int
shm_trunc(int fd, int size)
{
    struct shm_obj* shm_obj_pt;
    int pgs_c, i, j;
    char* pg;

    if (fd < 0 || fd >= SHMOBJ)
    {
        return -1;
    }

    acquire(&shm.lock);

    shm_obj_pt = &shm.shm_objs[fd];
    if (shm_obj_pt->size != 0)
    {
        // Size already set
        release(&shm.lock);
        return shm_obj_pt->size;
    }

    pgs_c = size / PGSIZE + (size % PGSIZE > 0 ? 1 : 0);
    if (pgs_c > SHMPGS)
    {
        // Too many pages requested
        release(&shm.lock);
        return -1;
    }

    for (i = 0; i < pgs_c; i++)
    {
        pg = kalloc();
        // Failed to allocate
        if (pg == 0)
        {
            // Free allocated
            for (j = 0; j < i; j++)
            {
                kfree(shm_obj_pt->pgs[j]);
            }
            release(&shm.lock);
            return -1;
        }
        memset(pg, 0, PGSIZE);
        shm_obj_pt->pgs[i] = pg;
    }

    shm_obj_pt->pgc = pgs_c;

    // Set this size last in case something fails above, so that object is not allocated
    shm_obj_pt->size = size;

    release(&shm.lock);
    return shm_obj_pt->size;
}

// int shm_map(int fd, void **va, int flags)
// Ovaj sistemski poziv obavlja mapiranje otvorenog shm objekta u virtuelni adresni prostor
// trenutnog procesa, i postavlja *va na početak tog prostora. Argument flags navodi da li se
// objekat mapira samo za čitanje, ili za čitanje i pisanje, i koristi iste konstante kao open -
// O_RDWR ili O_RDONLY. Nije dozvoljeno mapirati prostor samo za pisanje. Povratne vrednosti
// za ovaj sistemski poziv su:
// ● -1: neuspešno mapiranje objekta.
// ● 0: mapiranje shm objekta je završeno uspešno.

// Notes
// int mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm);
/*
 * pgdir is the page directory pointer (which you can get from the proc
 * structure for your process.
 *
 * va is a free virtual address you want to attach your page to (e.g.,
 * sz, perhaps rounded up).
 *
 * size is the size you are mapping, which in our case is a single page (i.e., PGSIZE).
 *
 * pa is the physical address which is the frame pointer you get from shm_table
 * (but pass it through the V2P macro)
 *
 * permissions are a set of PTE permissions.  Use PTE_W|PTE_U to say the
 * page is writeable and accessible to the user.
 */
int
shm_map(int fd, void **va, int flags)
{
    struct shm_obj* shm_obj_pt;
    struct proc* p;
    int i;
    uint sz, memstart;

    if (fd < 0 || fd >= SHMOBJ)
    {
        return -1;
    }

    acquire(&shm.lock);

    shm_obj_pt = &shm.shm_objs[fd];

    if (shm_obj_pt->name[0] == '\0') {
        // Not used
        release(&shm.lock);
        return -1;
    }

    p = myproc();
    // Not open or already mapped
    if (!p->shm[fd] || p->shm_mapped[fd] == 1)
    {
        release(&shm.lock);
        return -1;
    }
    
    memstart = SHMSTART + fd * SHMPGS * PGSIZE;
    // Do mapping
    for (i = 0; i < shm_obj_pt->pgc; i++)
    {
        if (i < shm_obj_pt->pgc - 1) {
            sz = PGSIZE;
        } else {
            sz = shm_obj_pt->size - (shm_obj_pt->pgc - 1) * PGSIZE;
        }
    
        if (mappages(
            p->pgdir,
            (void*) (memstart + i * PGSIZE),
            sz,
            V2P(shm_obj_pt->pgs[i]),
            flag(flags)) != 0)
        {
            // Failed to map
            release(&shm.lock);
            return -1;
        }
    }
    *va = (void*) memstart;

    p->shm_mapped[fd] = 1;
    p->shm_flags[fd] = flags;

    release(&shm.lock);
    return 0;
}

// int shm_close(int fd)
// Ovaj sistemski poziv zatvara shm objekat, tj. onemogućava dalje korišćenje ovog deskirptora
// unutar trenutnog procesa. Ovde se takođe obavlja odmapiranje objekta u slučaju da je bio
// mapiran. Ako je ovo poslednji proces koji je koristio objekat, onda ovaj sistemski poziv treba
// takođe da obriše objekat iz sistema. Povratne vrednosti za ovaj poziv su:
// ● -1: zatvaranje deskriptora nije bilo uspešno.
// ● 0: zatvaranje deskriptora je završeno uspešno.
int
shm_close(int fd)
{
    struct shm_obj* shm_obj_pt;
    struct proc* p;
    int i;

    if (fd < 0 || fd >= SHMOBJ)
    {
        return -1;
    }

    acquire(&shm.lock);

    shm_obj_pt = &shm.shm_objs[fd];

    if (shm_obj_pt->name[0] == '\0') {
        // Not used
        release(&shm.lock);
        return -1;
    }
    
    p = myproc();
    if (p->shm_mapped[fd] != 0)
    {
        // Unmap
        int memstart = SHMSTART + fd * SHMPGS * PGSIZE;
        for (i = 0; i < shm_obj_pt->pgc; i++)
        {
            unmappages(p->pgdir, (void*) (memstart + i * PGSIZE));
        }
    }
    p->shm_mapped[fd] = 0;
    p->shm[fd] = 0;

    shm_obj_pt->proc_cnt--;

    // Unused
    if (shm_obj_pt->proc_cnt == 0) {
        // cprintf("Cleared");
        unallocate_shm_obj(fd);
        clear_shm_obj(fd);
    }

    release(&shm.lock);
    return 0;
}

// Returns -1 if failure
int
shm_fork(struct proc* np, struct proc* curproc)
{
    int i, j, memstart, sz;

    acquire(&shm.lock);

    for (i = 0; i < SHMOBJ; i++) {
        if (curproc->shm[i] == 1) {
            np->shm[i] = 1;
            shm.shm_objs[i].proc_cnt++;
        }
        if (curproc->shm_mapped[i] != 0) {
            np->shm_mapped[i] = curproc->shm_mapped[i];
            np->shm_flags[i] = curproc->shm_flags[i];

            memstart = SHMSTART + i * SHMPGS * PGSIZE;
            // Do mapping
            for (j = 0; j < shm.shm_objs[i].pgc; j++)
            {
                if (j < shm.shm_objs[i].pgc - 1) {
                    sz = PGSIZE;
                } else {
                    sz = shm.shm_objs[i].size - (shm.shm_objs[i].pgc - 1) * PGSIZE;
                }
            
                if (mappages(
                    np->pgdir,
                    (void*) (memstart + j * PGSIZE),
                    sz,
                    V2P(shm.shm_objs[i].pgs[j]),
                    flag(np->shm_flags[i])) != 0)
                {
                    // Failed to map
                    release(&shm.lock);
                    return -1;
                }
            }
        }
    }

    release(&shm.lock);
    return 0;
}

void
shm_exit()
{
    struct proc* p;
    int i;

    p = myproc();
    for (i = 0; i < SHMOBJ; i++) {
        if (p->shm[i] == 1) {
            shm_close(i);
        }
    }
}

int
flag(int fcntl)
{
    if (fcntl == O_RDONLY) {
        return PTE_U;
    }
    if (fcntl == O_RDWR) {
        return PTE_W | PTE_U;
    }
    panic("Invalid fcntl");
}
