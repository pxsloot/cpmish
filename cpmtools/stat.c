#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "libcpm.h"

#define FCB_COUNT 512

uint8_t accumulator[4];
uint8_t ibp = 0;
DPB* dpb;
uint16_t bpb; /* bytes per block */

struct fe
{
    uint8_t filename[11];
    uint8_t extents;
    uint16_t blocks;
    uint16_t records;
};

struct fe files[FCB_COUNT];
struct fe* findex[FCB_COUNT]; /* pointers to above (for sorting) */

const uint8_t logical_device_names[] = "CON:RDR:PUN:LST:DEV:VAL:USR:DSK:";
enum { CON = 1, RDR, PUN, LST, DEV, VAL, USR, DSK };

const char physical_device_names[] =
    /* CON: */ "TTY:CRT:BAT:UC1:"
    /* RDR: */ "TTY:PTR:UR1:UR2:"
    /* PUN: */ "TTY:PTP:UP1:UP2:"
    /* LST: */ "TTY:CRT:LPT:UL1:";

const FCB wildcard_fcb_template =
{
    .dr = '?',
    .f = "???????????"
};

void print(const char* s)
{
    for (;;)
    {
        uint8_t b = *s++;
        if (!b)
            return;
        putchar(b);
    }
}

void crlf(void)
{
    print("\r\n");
}

void printx(const char* s)
{
    print(s);
    crlf();
}

/* 
 * Prints a 16-bit decimal number with optional left padding and configurable
 * precision. *.
 */
void printip(uint16_t v, bool pad, uint16_t precision)
{
    bool zerosup = true;
    while (precision)
    {
        uint8_t d = v / precision;
        v %= precision;
        precision /= 10;
        if (precision && zerosup && !d)
        {
            if (pad)
                putchar(' ');
        }
        else
        {
            zerosup = false;
            putchar('0' + d);
        }
    }
}

void printi(uint16_t v)
{
    printip(v, false, 10000);
}

/* Compares the accumulator with an array of uint8_t[4] words. Returns the
 * matching index plus one or 0. */
uint8_t compare_accumulator(const uint8_t* list, uint8_t length)
{
    uint8_t match = 1;
    while (length--)
    {
        bool m = true;
        for (uint8_t j=0; j<4; j++)
        {
            if (*list++ != accumulator[j])
                m = false;
        }
        if (m)
        {
            /* Matched! */
            return match;
        }
        match++;
    }
    return 0;
}

void select_disk(uint8_t d)
{
    cpm_select_drive(d);
    dpb = cpm_get_dpb();
    bpb = 1<<(dpb->bsh+7);
}

void select_fcb_disk(void)
{
    uint8_t drive = cpm_fcb.dr ? (cpm_fcb.dr-1) : cpm_get_current_drive();
    select_disk(drive);
}

uint16_t count_space(void)
{
    uint8_t* alloca = cpm_get_allocation_vector();
    uint16_t blocks = 0;
    for (uint16_t i=0; i<=dpb->dsm; i++)
    {
        bool bit = alloca[i >> 3] & (0x80 >> (i & 7));
        if (!bit)
            blocks++;
    }
    return blocks;
}

void print_free_space(void)
{
    uint16_t blocks = count_space();
    printi(blocks << (dpb->bsh - 3));
    print("/");
    printi((dpb->dsm+1) << (dpb->bsh - 3));
    print("kB");
}

/* Show status of all drives. */
void print_drive_status(void)
{
    uint16_t login = cpm_get_login_vector();
    uint16_t rodisk = cpm_get_rodisk_vector();

    uint8_t d = 0;
    while (login)
    {
        if (login & 1)
        {
            select_disk(d);
            putchar('A' + d);
            print(": R/");
            putchar((rodisk & 1) ? 'O' : 'W');
            print(", space: ");
            print_free_space();
            crlf();
        }

        login >>= 1;
        rodisk >>= 1;
        d++;
    }
}

/* Reads the next input token into the 4-byte accumulator. */
void scan(void)
{
    /* Skip whitespace. */

    while ((ibp != cpm_cmdlinelen) && (cpm_cmdline[ibp] == ' '))
        ibp++;

    uint8_t obp = 0;
    memset(accumulator, ' ', sizeof(accumulator));
    while (obp != 4)
    {
        if (ibp == cpm_cmdlinelen)
            return;

        uint8_t b = cpm_cmdline[ibp++];
        accumulator[obp++] = b;
        switch (b)
        {
            case 0:
            case ',':
            case ':':
            case '*':
            case '.':
            case '>':
            case '<':
            case '=':
                return;
        }
    }
}

void printipadded(uint16_t value)
{
    printip(value, true, 10000);
}

void get_detailed_drive_status(void)
{
    print("    ");
    putchar(cpm_get_current_drive() + 'A');
    putchar(':');
    printx(" Drive Characteristics");

    uint16_t rpb = 1<<dpb->bsh;
    uint16_t rpd = (dpb->dsm+1) * rpb;
    if ((rpd == 0) && (rpb != 0))
        print("65536");
    else
        printipadded(rpd);
    printx(": 128 byte record capacity");

    printipadded(count_space());
    printx(": kilobyte drive capacity");

    printipadded(dpb->drm+1);
    printx(": 32 byte directory entries");

    printipadded(dpb->cks);
    printx(": checked directory entries");

    printipadded((dpb->exm+1)*128);
    printx(": records per extent");

    printipadded(dpb->spt);
    printx(": sectors per track");

    printipadded(dpb->off);
    printx(": reserved tracks");
}

int index_sort_cb(const void* left, const void* right)
{
    const struct fe** leftp = (const struct fe**) left;
    const struct fe** rightp = (const struct fe**) right;
    return memcmp((*leftp)->filename, (*rightp)->filename, 11);
}

void print_filename(uint8_t* filename)
{
    for (uint8_t i=0; i<11; i++)
    {
        uint8_t b = *filename++ & 0x7f;
        if (b != ' ')
        {
            if (i == 8)
                putchar('.');
            putchar(b);
        }
    }
}

void file_manipulation(void)
{
    /* Options are passed as the second word on the command line, which the
     * CPP parses as a filename and writes into cpm_fcb2. There will now be
     * a short pause for me to be ill. */

    const static uint8_t command_names[] = "$S  $R/O$R/W$SYS$DIR";
    enum { LIST = 0, LIST_WITH_SIZE, SET_RO, SET_RW, SET_SYS, SET_DIR };
    memcpy(accumulator, cpm_fcb2.f, 4);
    uint8_t command = compare_accumulator(command_names, sizeof(command_names)/4);

    select_fcb_disk();
    cpm_fcb.ex = '?'; /* find all extents, not just the first */
    uint16_t count = 0;
    uint8_t r = cpm_findfirst(&cpm_fcb);
    while (r != 0xff)
    {
        DIRE* de = (DIRE*)0x80 + r;
        struct fe* fe = files;

        /* Try to find the file in the array. */

        uint16_t i = count;
        while (i)
        {
            if (memcmp(fe->filename, de->f, 11) == 0)
                break;
            fe++;
            i--;
        }

        if (!i)
        {
            /* Not found --- add it. */

            memset(fe, 0, sizeof(*fe));
            memcpy(fe->filename, de->f, 11);
            findex[count] = fe;

            count++;
            if (count == FCB_COUNT)
            {
                printx("Too many files!");
                return;
            }
        }

        fe->extents++;
        fe->records += de->rc + (de->ex & dpb->exm)*128;
        if (dpb->dsm < 256)
        {
            /* 8-bit allocation map. */
            for (uint8_t j=0; j<16; j++)
            {
                if (de->al.al8[j])
                    fe->blocks++;
            }
        }
        else
        {
            /* 16-bit allocation map. */
            for (uint8_t j=0; j<8; j++)
            {
                if (de->al.al16[j])
                    fe->blocks++;
            }
        }

        r = cpm_findnext(&cpm_fcb);
    }

    switch (command)
    {
        case LIST:
        case LIST_WITH_SIZE:
        {
            qsort(findex, count, sizeof(void*), index_sort_cb);

            uint8_t current_drive = 'A' + cpm_get_current_drive();
            if (command == LIST_WITH_SIZE)
                print("  Size");
            printx(" Recs   Bytes  Ext Acc");
            struct fe** fep = findex;
            while (count--)
            {
                struct fe* f = *fep++;
                if (command == LIST_WITH_SIZE)
                {
                    memset(&cpm_fcb, 0, sizeof(FCB));
                    memcpy(cpm_fcb.f, f->filename, 11);
                    cpm_seek_to_end(&cpm_fcb);
                    if (cpm_fcb.r2)
                        print("65536");
                    else
                        printipadded(cpm_fcb.r);
                    putchar(' ');
                }
                printipadded(f->records);
                putchar(' ');
                printipadded(f->blocks << (dpb->bsh - 3));
                print("kB ");
                printip(f->extents, true, 1000);
                print((f->filename[8] & 0x80) ? " R/O " : " R/W ");
                putchar(current_drive);
                putchar(':');
                if (f->filename[9] & 0x80)
                    putchar('(');
                print_filename(f->filename);
                if (f->filename[9] & 0x80)
                    putchar(')');
                crlf();
                if (cpm_get_console_status())
                    return;
            }
            print("Bytes remaining on ");
            putchar(current_drive);
            print(": ");
            print_free_space();
            crlf();
            break;
        }

        case SET_RO:
        case SET_RW:
        case SET_SYS:
        case SET_DIR:
        {
            uint8_t attrbyte = ((command == SET_RO) || (command == SET_RW)) ? 8 : 9;
            uint8_t attrflag = ((command == SET_RO) || (command == SET_SYS)) ? 0x80 : 0x00;

            struct fe* fe = files;
            while (count--)
            {
                print_filename(fe->filename);
                memset(&cpm_fcb, 0, sizeof(FCB));
                memcpy(cpm_fcb.f, fe->filename, 11);
                cpm_fcb.f[attrbyte] = (cpm_fcb.f[attrbyte] & 0x7f) | attrflag;
                cpm_set_file_attributes(&cpm_fcb);

                print(" set to ");
                const uint8_t* p = &command_names[(command-1)*4] + 1;
                putchar(*p++);
                putchar(*p++);
                putchar(*p);
                crlf();
                if (cpm_get_console_status())
                    return;

                fe++;
            }

            break;
        }
    }
}

/* Handles the A:=R/O and A: DSK: cases. */
void set_drive_status(void)
{
    scan(); /* drive is already in fcb.dr */
    scan(); /* read = */
    if (accumulator[0] == '=')
    {
        scan(); /* get assignment */
        if (compare_accumulator("R/O ", 1))
        {
            select_fcb_disk();
            cpm_write_protect_drive();
        }
        else
            printx("Invalid disk assignment (only R/O is supported)");
    }
    else
    {
        /* Not a disk assignment --- the user must be trying to stat a file. */

        select_fcb_disk();
        if (compare_accumulator(logical_device_names, 8) == DSK)
            get_detailed_drive_status();
        else
            file_manipulation();
    }
}

void print_device_name(const char* p)
{
    for (;;)
    {
        uint8_t c = *p++;
        putchar(c);
        if (c == ':')
            break;
    }
}

bool change_device_assignment(uint8_t logical)
{
    scan();
    if (accumulator[0] != '=')
    {
        print("Bad delimiter");
        return true;
    }

    scan();
    uint8_t physical = compare_accumulator(&physical_device_names[logical*16], 4) - 1;
    if (physical == 0xff)
    {
        printx("Invalid assignment");
        return true;
    }

    uint8_t b = 3;
    while (logical--)
    {
        b <<= 2;
        physical <<= 2;
    }
    cpm_iobyte &= ~b;
    cpm_iobyte |= physical;
    return false;
}

void show_device_assignments(void)
{
    uint8_t b = cpm_iobyte;
    const char* lp = logical_device_names;
    const char* pp = physical_device_names;

    for (uint8_t i=0; i<4; i++)
    {
        print_device_name(lp);
        print(" is ");
        print_device_name(pp + (b & 3)*4);
        crlf();
        b >>= 2;
        lp += 4;
        pp += 16;
    }
}

void show_help(void)
{
    printx(
        "Set disk to read only:  stat d:=R/O\r\n"
        "Set file attributes:    stat d:filename.typ $R/O / $R/W / $SYS / $DIR\r\n"
        "Get file attributes:    stat d:filename.typ [ $S ]\r\n"
        "Show disk info:         stat DSK: / d: DSK:\r\n"
        "Show user number usage: stat USR:\r\n"
        "Show device mapping:    stat DEV:"
    );

    const char* lp = logical_device_names;
    const char* pp = physical_device_names;
    for (uint8_t i=0; i<4; i++)
    {
        print("Set device mapping:     stat ");
        print_device_name(lp);
        lp += 4;
        putchar('=');

        for (uint8_t j=0; j<4; j++)
        {
            if (j)
                print(" / ");
            print_device_name(pp);
            pp += 4;
        }
        crlf();
    }
}

void show_user_numbers(void)
{
    static FCB wildcard_fcb;
    memcpy(&wildcard_fcb, &wildcard_fcb_template, sizeof(FCB));

    print("Active user: ");
    printi(cpm_get_current_user());
    crlf();
    print("Active files:");

    DIRE* data = (DIRE*) files; /* horrible, but it's unused and big enough */
    cpm_set_dma(data);
    static uint8_t users[32];
    memset(users, 0, sizeof(users));

    uint8_t r = cpm_findfirst(&wildcard_fcb);
    while (r != 0xff)
    {
        DIRE* found = &data[r];
        /* On disk, dr contains the user number */
        if (found->us != 0xe5)
            users[found->us & 0x1f] = 1;
        r = cpm_findnext(&wildcard_fcb);
    }

    for (uint8_t i=0; i<32; i++)
    {
        if (users[i])
        {
            putchar(' ');
            printi(i);
        }
    }

    crlf();
}

/* Handle device assignment, querying, and miscellaneous other things */
bool device_manipulation(void)
{
    uint8_t items = 0;
    for (;;)
    {
        scan();
        uint8_t i = compare_accumulator(logical_device_names, 8);
        if (!i)
            return items;
        items++;

        switch (i)
        {
            default:
                if (change_device_assignment(i-1))
                    return true;
                break;

            case DEV:
                show_device_assignments();
                break;

            case VAL:
                show_help();
                break;

            case USR: /* Show user number usage on the current disk */
                select_fcb_disk();
                show_user_numbers();
                break;

            case DSK:
                select_fcb_disk();
                get_detailed_drive_status();
                break;
        }

        scan();
        if (accumulator[0] == ' ')
            return true;
        if (accumulator[0] != ',')
            goto bad_delimiter;
    }

bad_delimiter:
    print("Bad delimiter '");
    printi(accumulator[0]);
    printx("'");
    return true;
}

void main(void)
{
    if (!cpm_fcb.dr && (cpm_fcb.f[0] == ' '))
        print_drive_status();
    else if (cpm_fcb.dr)
        set_drive_status();
    else if (!device_manipulation())
        file_manipulation();
}