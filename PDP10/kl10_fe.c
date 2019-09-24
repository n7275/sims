/* kl10_fe.c: KL-10 front end (console terminal) simulator

   Copyright (c) 2013-2019, Richard Cornwell

   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
   RICHARD CORNWELL BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Richard Cornwell shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Richard Cornwell

*/

#include "kx10_defs.h"
#include "sim_sock.h"
#include "sim_tmxr.h"
#include <ctype.h>

#if KL
#define UNIT_DUMMY      (1 << UNIT_V_UF)

#define DTE_DEVNUM       0200

/* DTE10 CONI bits */

#define DTE_RM         00100000    /* Restricted mode */
#define DTE_D11        00040000    /* Dead-11 */
#define DTE_11DB       00020000    /* TO11 Door bell request */
#define DTE_10DB       00001000    /* TO10 Door bell request */
#define DTE_11ER       00000400    /* Error during TO11 transfer */
#define DTE_11DN       00000100    /* TO 11 transfer done */
#define DTE_10DN       00000040    /* TO 10 transfer done */
#define DTE_10ER       00000020    /* Error during TO10 transfer */
#define DTE_PIE        00000010    /* DTE PI enabled */
#define DTE_PIA        00000007    /* PI channel assigment */

/* internal flags */
#define DTE_11RELD     01000000    /* Reload 11. */
#define DTE_TO11       02000000    /* Transfer to 11 */
#define DTE_SEC        04000000    /* In secondary protocol */
#define DTE_IND        010000000   /* Next transfer will be indirect */
#define DTE_SIND       020000000   /* Send indirect data next */

/* DTE CONO bits */
#define DTE_CO11DB     0020000     /* Set TO11 Door bell */
#define DTE_CO11CR     0010000     /* Clear reload 11 button */
#define DTE_CO11SR     0004000     /* Set reload 11 button */
#define DTE_CO10DB     0001000     /* Clear TO10 Door bell */
#define DTE_CO11CL     0000100     /* Clear TO11 done and error */
#define DTE_CO10CL     0000040     /* Clear TO10 done and error */
#define DTE_PIENB      0000020     /* Load PI and enable bit */

/* DTE DATAO */
#define DTE_TO10IB     010000      /* Interrupt after transfer */
#define DTE_TO10BC     007777      /* Byte count for transfer */

/* Secondary protocol addresses */
#define SEC_DTFLG      0444        /* Operation complete flag */
#define SEC_DTCLK      0445        /* Clock interrupt flag */
#define SEC_DTCI       0446        /* Clock interrupt instruction */
#define SEC_DTT11      0447        /* 10 to 11 argument */
#define SEC_DTF11      0450        /* 10 from 11 argument */
#define SEC_DTCMD      0451        /* To 11 command word */
#define SEC_DTSEQ      0452        /* Operation sequence number */
#define SEC_DTOPR      0453        /* Operational DTE # */
#define SEC_DTCHR      0454        /* Last typed character */
#define SEC_DTMTD      0455        /* Monitor tty output complete flag */
#define SEC_DTMTI      0456        /* Monitor tty input flag */
#define SEC_DTSWR      0457        /* 10 switch register */

#define SEC_PGMCTL     00400
#define SEC_ENDPASS    00404
#define SEC_LOOKUP     00406
#define SEC_RDWRD      00407
#define SEC_RDBYT      00414
#define SEC_ESEC       00440
#define SEC_EPRI       00500
#define SEC_ERTM       00540
#define SEC_CLKCTL     01000
#define SEC_CLKOFF     01000
#define SEC_CLKON      01001
#define SEC_CLKWT      01002
#define SEC_CLKRD      01003
#define SEC_RDSW       01400
#define SEC_CLRDDT     03000
#define SEC_SETDDT     03400
#define SEC_MONO       04000
#define SEC_MONON      04400
#define SEC_SETPRI     05000
#define SEC_RTM        05400
#define SEC_CMDMSK     07400
#define DTE_MON        00000001     /* Save in unit1 STATUS */
#define SEC_CLK        00000002     /* Clock enabled */
#define ITS_ON         00000004     /* ITS Is alive */

/* Primary or Queued protocol addresses */
#define PRI_CMTW_0     0
#define PRI_CMTW_PPT   1            /* Pointer to com region */
#define PRI_CMTW_STS   2            /* Status word */
#define PRI_CMT_PWF    SMASK        /* Power failure */
#define PRI_CMT_L11    BIT1         /* Load 11 */
#define PRI_CMT_INI    BIT2         /* Init */
#define PRI_CMT_TST    BIT3         /* Valid examine bit */
#define PRI_CMT_QP     020000000LL  /* Do Queued protocol */
#define PRI_CMT_FWD    001000000LL  /* Do full word transfers */
#define PRI_CMT_IP     RSIGN        /* Indirect transfer */
#define PRI_CMT_TOT    0200000LL    /* TOIT bit */
#define PRI_CMT_10IC   0177400LL    /* TO10 IC for queued transfers */
#define PRI_CMT_11IC   0000377LL    /* TO11 IC for queued transfers */
#define PRI_CMTW_CNT   3            /* Queue Count */
#define PRI_CMTW_KAC   5            /* Keep alive count */

#define PRI_EM2EI      001          /* Initial message to 11 */
#define PRI_EM2TI      002          /* Replay to initial message. */
#define PRI_EMSTR      003          /* String data */
#define PRI_EMLNC      004          /* Line-Char */
#define PRI_EMRDS      005          /* Request device status */
#define PRI_EMHDS      007          /* Here is device status */
#define PRI_EMRDT      011          /* Request Date/Time */
#define PRI_EMHDR      012          /* Here is date and time */
#define PRI_EMFLO      013          /* Flush output */
#define PRI_EMSNA      014          /* Send all (ttys) */
#define PRI_EMDSC      015          /* Dataset connect */
#define PRI_EMHUD      016          /* Hang up dataset */
#define PRI_EMACK      017          /* Acknowledge line */
#define PRI_EMXOF      020          /* XOFF line */
#define PRI_EMXON      021          /* XON line */
#define PRI_EMHLS      022          /* Here is line speeds */
#define PRI_EMHLA      023          /* Here is line allocation */
#define PRI_EMRBI      024          /* Reboot information */
#define PRI_EMAKA      025          /* Ack ALL */
#define PRI_EMTDO      026          /* Turn device On/Off */
#define PRI_EMEDR      027          /* Enable/Disable line */
#define PRI_EMLDR      030          /* Load LP RAM */
#define PRI_EMLDV      031          /* Load LP VFU */

#define PRI_EMCTY      001          /* Device code for CTY */
#define PRI_EMDL1      002          /* DL11 */
#define PRI_EMDH1      003          /* DH11 #1 */
#define PRI_EMDLS      004          /* DLS (all ttys combined) */
#define PRI_EMLPT      005          /* Front end LPT */
#define PRI_EMCDR      006          /* CDR */
#define PRI_EMCLK      007          /* Clock */
#define PRI_EMFED      010          /* Front end device */

#if KL_ITS
/* ITS Timesharing protocol locations */
#define ITS_DTEVER     0400         /* Protocol version and number of devices */
#define ITS_DTECHK     0401         /* Increment at 60Hz. Ten setom 2 times per second */
#define ITS_DTEINP     0402         /* Input from 10 to 11. Line #, Count */
#define ITS_DTEOUT     0403         /* Output from 10 to 11 Line #, Count */
#define ITS_DTELSP     0404         /* Line # to set speed of */
#define ITS_DTELPR     0405         /* Parameter */
#define ITS_DTEOST     0406         /* Line # to start output on */
#define ITS_DTETYI     0410         /* Received char (Line #, char) */
#define ITS_DTEODN     0411         /* Output done (Line #, buffer size) */
#define ITS_DTEHNG     0412         /* Hangup/dialup */
#endif

#define TMR_RTC        2

extern int32 tmxr_poll;
t_stat dte_devio(uint32 dev, uint64 *data);
int    dte_devirq(uint32 dev, int addr);
void   dte_second(UNIT *uptr);
void   dte_primary(UNIT *uptr);
#if KL_ITS
void   dte_its(UNIT *uptr);
#endif
void   dte_transfer(UNIT *uptr);
void   dte_function(UNIT *uptr);
int    dte_start(UNIT *uptr);
int    dte_queue(UNIT *uptr, int func, int dev, int dcnt, uint16 *data);
t_stat dtei_svc (UNIT *uptr);
t_stat dteo_svc (UNIT *uptr);
t_stat dtertc_srv(UNIT * uptr);
t_stat dte_reset (DEVICE *dptr);
t_stat dte_stop_os (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat tty_set_mode (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat dte_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
const char *dte_description (DEVICE *dptr);
extern uint64  SW;                                   /* Switch register */

#if KL_ITS
#define QITS         (cpu_unit[0].flags & UNIT_ITSPAGE)
#else
#define QITS         0
#endif

#define STATUS            u3
#define CNT               u4
#define CHHOLD            u5

extern uint32  eb_ptr;
static int32   rtc_tps = 60;
uint16         rtc_tick;
uint16         rtc_wait = 0;

struct _dte_queue {
    int         dptr;      /* Pointer to working item */
    uint16      cnt;       /* Number of bytes in packet */
    uint16      func;      /* Function code */
    uint16      dev;       /* Dev code */
    uint16      spare;     /* Dev code */
    uint16      dcnt;      /* Data count */
    uint16      data[256]; /* Data packet */
    uint16      sdev;      /* Secondary device code */
} dte_in[32], dte_out[32];

int dte_in_ptr;
int dte_in_cmd;
int dte_out_ptr;
int dte_out_res;
int dte_base;            /* Base */
int dte_off;             /* Our offset */
int dte_dt10_off;        /* Offset to 10 deposit region */
int dte_et10_off;        /* Offset to 10 examine region */
int dte_et11_off;        /* Offset to 11 examine region */
int dte_proc_num;        /* Our processor number */

struct _buffer {
    int      in_ptr;     /* Insert pointer */
    int      out_ptr;    /* Remove pointer */
    char     buff[256];  /* Buffer */
} cty_in, cty_out;

int cty_data;

DIB dte_dib[] = {
    { DTE_DEVNUM|000, 1, dte_devio, dte_devirq},
};

MTAB dte_mod[] = {
    { UNIT_DUMMY, 0, NULL, "STOP", &dte_stop_os },
    { TT_MODE, TT_MODE_UC, "UC", "UC", &tty_set_mode },
    { TT_MODE, TT_MODE_7B, "7b", "7B", &tty_set_mode },
    { TT_MODE, TT_MODE_8B, "8b", "8B", &tty_set_mode },
    { TT_MODE, TT_MODE_7P, "7p", "7P", &tty_set_mode },
    { 0 }
    };

UNIT dte_unit[] = {
    { UDATA (&dteo_svc, TT_MODE_7B, 0), 10000 },
    { UDATA (&dtei_svc, TT_MODE_7B|UNIT_DIS, 0), 10000 },
    { UDATA (&dtertc_srv, UNIT_IDLE|UNIT_DIS, 0), 1000 }
    };


DEVICE dte_dev = {
    "CTY", dte_unit, NULL, dte_mod,
    3, 10, 31, 1, 8, 8,
    NULL, NULL, &dte_reset,
    NULL, NULL, NULL, &dte_dib, DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &dte_help, NULL, NULL, &dte_description
    };



#ifndef NUM_DEVS_LP
#define NUM_DEVS_LP 0
#endif

#if (NUM_DEVS_LP > 0)

#define COL      u4
#define POS      u5
#define LINE     u6

#define MARGIN   6

#define UNIT_V_CT    (UNIT_V_UF + 0)
#define UNIT_UC      (1 << UNIT_V_CT)
#define UNIT_CT      (3 << UNIT_V_CT)



t_stat          lpt_svc (UNIT *uptr);
t_stat          lpt_reset (DEVICE *dptr);
t_stat          lpt_attach (UNIT *uptr, CONST char *cptr);
t_stat          lpt_detach (UNIT *uptr);
t_stat          lpt_setlpp(UNIT *, int32, CONST char *, void *);
t_stat          lpt_getlpp(FILE *, UNIT *, int32, CONST void *);
t_stat          lpt_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
                         const char *cptr);
const char     *lpt_description (DEVICE *dptr);

char            lpt_buffer[134 * 3];

struct _buffer lpt_queue;

/* LPT data structures

   lpt_dev      LPT device descriptor
   lpt_unit     LPT unit descriptor
   lpt_reg      LPT register list
*/

UNIT lpt_unit = {
    UDATA (&lpt_svc, UNIT_SEQ+UNIT_ATTABLE+UNIT_TEXT, 66), 100
    };

REG lpt_reg[] = {
    { BRDATA(BUFF, lpt_buffer, 16, 8, sizeof(lpt_buffer)), REG_HRO},
    { NULL }
};

MTAB lpt_mod[] = {
    {UNIT_CT, 0, "Lower case", "LC", NULL},
    {UNIT_CT, UNIT_UC, "Upper case", "UC", NULL},
    {MTAB_XTD|MTAB_VUN|MTAB_VALR, 0, "LINESPERPAGE", "LINESPERPAGE",
        &lpt_setlpp, &lpt_getlpp, NULL, "Number of lines per page"},
    { 0 }
};

DEVICE lpt_dev = {
    "LPT", &lpt_unit, lpt_reg, lpt_mod,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &lpt_reset,
    NULL, &lpt_attach, &lpt_detach,
    NULL, DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &lpt_help, NULL, NULL, &lpt_description
};
#endif

#ifndef NUM_DEVS_TTY
#define NUM_DEVS_TTY 0
#endif

#if (NUM_DEVS_TTY > 0)

struct _buffer tty_out[NUM_LINES_TTY], tty_in[NUM_LINES_TTY];
struct _buffer tty_done, tty_hang;
TMLN     tty_ldsc[NUM_LINES_TTY] = { 0 };            /* Line descriptors */
TMXR     tty_desc = { NUM_LINES_TTY, 0, 0, tty_ldsc };
int      tty_connect[NUM_LINES_TTY];;
int      tty_enable = 0;
extern int32 tmxr_poll;

t_stat ttyi_svc (UNIT *uptr);
t_stat ttyo_svc (UNIT *uptr);
t_stat tty_reset (DEVICE *dptr);
t_stat tty_set_modem (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat tty_show_modem (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat tty_setnl (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat tty_set_log (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat tty_set_nolog (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat tty_show_log (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat tty_attach (UNIT *uptr, CONST char *cptr);
t_stat tty_detach (UNIT *uptr);
t_stat tty_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
        const char *cptr);
const char *tty_description (DEVICE *dptr);

/* TTY data structures

   tty_dev      TTY device descriptor
   tty_unit     TTY unit descriptor
   tty_reg      TTY register list
*/

UNIT tty_unit[] = {
    { UDATA (&ttyi_svc, TT_MODE_7B+UNIT_IDLE+UNIT_DISABLE+UNIT_ATTABLE, 0), KBD_POLL_WAIT},
    { UDATA (&ttyo_svc, TT_MODE_7B+UNIT_IDLE+UNIT_DIS, 0), KBD_POLL_WAIT},
    };

REG tty_reg[] = {
    { DRDATA (TIME, tty_unit[0].wait, 24), REG_NZ + PV_LEFT },
    { NULL }
    };

MTAB tty_mod[] = {
    { TT_MODE, TT_MODE_KSR, "KSR", "KSR", NULL },
    { TT_MODE, TT_MODE_7B,  "7b",  "7B",  NULL },
    { TT_MODE, TT_MODE_8B,  "8b",  "8B",  NULL },
    { TT_MODE, TT_MODE_7P,  "7p",  "7P",  NULL },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 1, NULL, "DISCONNECT",
        &tmxr_dscln, NULL, &tty_desc, "Disconnect a specific line" },
    { UNIT_ATT, UNIT_ATT, "SUMMARY", NULL,
        NULL, &tmxr_show_summ, (void *) &tty_desc, "Display a summary of line states" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 1, "CONNECTIONS", NULL,
        NULL, &tmxr_show_cstat, (void *) &tty_desc, "Display current connections" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "STATISTICS", NULL,
        NULL, &tmxr_show_cstat, (void *) &tty_desc, "Display multiplexer statistics" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "LINES", "LINES=n",
        &tty_setnl, &tmxr_show_lines, (void *) &tty_desc, "Set number of lines" },
    { MTAB_XTD|MTAB_VDV|MTAB_NC, 0, NULL, "LOG=n=file",
        &tty_set_log, NULL, (void *)&tty_desc },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, NULL, "NOLOG",
        &tty_set_nolog, NULL, (void *)&tty_desc, "Disable logging on designated line" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "LOG", NULL,
        NULL, &tty_show_log, (void *)&tty_desc, "Display logging for all lines" },
    { 0 }
    };

DEVICE tty_dev = {
    "TTY", tty_unit, tty_reg, tty_mod,
    2, 10, 31, 1, 8, 8,
    &tmxr_ex, &tmxr_dep, &tty_reset,
    NULL, &tty_attach, &tty_detach,
    NULL, DEV_NET | DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &tty_help, NULL, NULL, &tty_description
    };
#endif


t_stat dte_devio(uint32 dev, uint64 *data) {
     uint32     res;
     switch(dev & 3) {
     case CONI:
        *data = (uint64)(dte_unit[0].STATUS) & RMASK;
        sim_debug(DEBUG_CONI, &dte_dev, "CTY %03o CONI %06o\n", dev, (uint32)*data);
        break;
     case CONO:
         res = (uint32)(*data & RMASK);
         clr_interrupt(dev);
         if (res & DTE_PIENB) {
             dte_unit[0].STATUS &= ~(DTE_PIA|DTE_PIE);
             dte_unit[0].STATUS |= res & (DTE_PIA|DTE_PIE);
         }
         if (res & DTE_CO11CL) 
             dte_unit[0].STATUS &= ~(DTE_11DN|DTE_11ER);
         if (res & DTE_CO10CL) 
             dte_unit[0].STATUS &= ~(DTE_10DN|DTE_10ER);
         if (res & DTE_CO10DB)
             dte_unit[0].STATUS &= ~(DTE_10DB);
         if (res & DTE_CO11CR)
             dte_unit[0].STATUS &= ~(DTE_11RELD);
         if (res & DTE_CO11SR)
             dte_unit[0].STATUS |= (DTE_11RELD);
         if (res & DTE_CO11DB) {
sim_debug(DEBUG_CONO, &dte_dev, "CTY Ring 11 DB\n");
             dte_unit[0].STATUS |= DTE_11DB;
             sim_activate(&dte_unit[0], 200);
         }
         if (dte_unit[0].STATUS & DTE_PIE && 
             dte_unit[0].STATUS & (DTE_10DB|DTE_11DN|DTE_10DN|DTE_11ER|DTE_10ER))
             set_interrupt(dev, dte_unit[0].STATUS);
         sim_debug(DEBUG_CONO, &dte_dev, "CTY %03o CONO %06o %06o\n", dev, (uint32)*data, PC);
         break;
     case DATAI:
         sim_debug(DEBUG_DATAIO, &dte_dev, "CTY %03o DATAI %06o\n", dev, (uint32)*data);
         break;
    case DATAO:
         dte_unit[0].CNT = (*data & (DTE_TO10IB|DTE_TO10BC));
         dte_unit[0].STATUS |= DTE_TO11;
         sim_activate(&dte_unit[0], 10);
         sim_debug(DEBUG_DATAIO, &dte_dev, "CTY %03o DATAO %06o\n", dev, (uint32)*data);
         break;
    }
    return SCPE_OK;
}

/* Handle KL style interrupt vectors */
int
dte_devirq(uint32 dev, int addr) {
    return 0142; 
}

/* Handle TO11 interrupts */
t_stat dteo_svc (UNIT *uptr)
{
    t_stat  r;

    /* Did the 10 knock? */
    if (uptr->STATUS & DTE_11DB) {
        /* If in secondary mode, do that protocol */
        if (uptr->STATUS & DTE_SEC)
            dte_second(uptr);
        else
            dte_primary(uptr);   /* Retrieve data */
    } else if (uptr->STATUS & DTE_TO11) {
        /* Does 10 want us to send it what we have? */
        dte_transfer(uptr);
    }
    return SCPE_OK;
}

/* Handle secondary protocol */
void dte_second(UNIT *uptr) {
    uint64   word;
    int32    ch;
    uint32   base = 0;
    t_stat  r;

#if KI_22BIT
#if KL_ITS
    if ((cpu_unit[0].flags & UNIT_ITSPAGE) == 0)
#endif
    base = eb_ptr;
#endif
    /* read command */ 
    word = M[SEC_DTCMD + base];
#if KL_ITS
    if (word == 0 && QITS && (uptr->STATUS & ITS_ON) != 0) {
        dte_its(uptr);
//        uptr->STATUS |= DTE_10DB;
        uptr->STATUS &= ~DTE_11DB;
        return;
    }
#endif
    /* Do it */
    sim_debug(DEBUG_DETAIL, &dte_dev, "CTY secondary %012llo\n", word);
    switch(word & SEC_CMDMSK) {
    default:
    case SEC_MONO:  /* Ouput character in monitor mode */
         if (((cty_out.in_ptr + 1) & 0xff) == cty_out.out_ptr) {
            sim_activate(uptr, 1000);
            return;
         }
         ch = (int32)(word & 0177);
         ch = sim_tt_outcvt( ch, TT_GET_MODE(uptr->flags));
         cty_out.buff[cty_out.in_ptr] = (char)(word & 0x7f);
         cty_out.in_ptr = (cty_out.in_ptr + 1) & 0xff;
         M[SEC_DTCHR + base] = ch;
         M[SEC_DTMTD + base] = FMASK;
         M[SEC_DTF11 + base] = 0;
         break;
     case SEC_SETPRI:
enter_pri:
         if (Mem_examine_word(0, 0, &word))
             break;
         dte_proc_num = (word >> 24) & 037;
         dte_base = dte_proc_num + 1;
         dte_off = dte_base + (word & 0177777);
         dte_dt10_off = 16;
         dte_et10_off = dte_dt10_off + 16;
         dte_et11_off = dte_base + 16;
         uptr->STATUS &= ~DTE_SEC;
         dte_unit[1].STATUS &= ~DTE_SEC;
         dte_in_ptr = dte_out_ptr = 0;
         dte_in_cmd = dte_out_res = 0;
         /* Start input process */
         break;
     case SEC_SETDDT: /* Read character from console */
         if (cty_in.in_ptr == cty_in.out_ptr) {
             sim_activate(uptr, 100);
             return;
         }
         ch = cty_in.buff[cty_in.out_ptr];
         cty_in.out_ptr = (cty_in.out_ptr + 1) & 0xff;
         M[SEC_DTF11 + base] = 0177 & ch;
         M[SEC_DTMTI + base] = FMASK;
         break;
     case SEC_CLRDDT: /* Clear DDT input mode */
         uptr->STATUS &= ~DTE_MON;
//         sim_cancel(&dte_unit[1]);
         break;
     case SEC_MONON:
         uptr->STATUS |= DTE_MON;
//         sim_activate(&dte_unit[1], 100);
         break;
     case SEC_RDSW:  /* Read switch register */
         M[SEC_DTSWR + base] = SW;
         M[SEC_DTF11 + base] = SW;
         break;

     case SEC_PGMCTL: /* Program control: Used by KLDCP */
         switch(word) {
         case SEC_ENDPASS:
         case SEC_LOOKUP:
         case SEC_RDWRD:
         case SEC_RDBYT:
              break;
         case SEC_ESEC:
              goto enter_pri;
         case SEC_EPRI:
         case SEC_ERTM:
              break;
         }
         break;
     case SEC_CLKCTL: /* Clock control: Used by KLDCP */
         switch(word) {
         case SEC_CLKOFF:
              dte_unit[2].STATUS &= ~SEC_CLK;
              break;
         case SEC_CLKWT:
              rtc_wait = (uint16)(M[SEC_DTT11 + base] & 0177777);
         case SEC_CLKON:
              dte_unit[2].STATUS |= SEC_CLK;
              rtc_tick = 0;
              break;
         case SEC_CLKRD:
              M[SEC_DTF11+base] = rtc_tick;
              break;
         }
         break;
     }
     /* Acknowledge command */
     M[SEC_DTCMD + base] = 0;
     M[SEC_DTFLG + base] = FMASK;
     uptr->STATUS |= DTE_10DB;
     uptr->STATUS &= ~DTE_11DB;
}

#if KL_ITS
void dte_its(UNIT *uptr) {
     uint64     word;
     char       ch;
     uint16     data;
     int        cnt;
     int        ln;
     t_stat     r;

     /* Check for output Start */
     word = M[ITS_DTEOST];
     if ((word & SMASK) == 0) {
         if (((tty_done.in_ptr + 1) & 0xff) != tty_done.out_ptr) {
              tty_done.buff[tty_done.in_ptr] = (char)(word & 0xff);
              tty_done.in_ptr = (tty_done.in_ptr + 1) & 0xff;
              M[ITS_DTEOST] = FMASK;
              sim_debug(DEBUG_DETAIL, &dte_dev, "CTY ITS DTEOST = %012llo\n", word);
         }
     }
     /* Check for input Start */
     word = M[ITS_DTEINP];
     if ((word & SMASK) == 0) {
         M[ITS_DTEINP] = FMASK;
         sim_debug(DEBUG_DETAIL, &dte_dev, "CTY ITS DTEINP = %012llo\n", word);
     }
     /* Check for output Start */
     word = M[ITS_DTEOUT];
     if ((word & SMASK) == 0) {
         cnt = word & 017777;
         ln = ((word >> 18) & 077) - 1;
         sim_debug(DEBUG_DETAIL, &dte_dev, "CTY ITS DTEOUT = %012llo\n", word);
         while (cnt > 0) {
             if (ln < 0) {
                 if (!Mem_read_byte(0, &data)) 
                    return;
                 ch = (data >> 8) & 0177;
                 sim_debug(DEBUG_DETAIL, &dte_dev, "CTY type %x\n", ch);
                 ch = sim_tt_outcvt( ch, TT_GET_MODE(uptr->flags));
                 if ((r = sim_putchar_s (ch)) != SCPE_OK) /* Output errors */
                      return;
                 cnt--;
                 if (cnt) {
                     ch = data & 0177;
                     sim_debug(DEBUG_DETAIL, &dte_dev, "CTY type %x\n", ch);
                     ch = sim_tt_outcvt( ch, TT_GET_MODE(uptr->flags));
                     if ((r = sim_putchar_s (ch)) != SCPE_OK) /* Output errors */
                          return;
                     cnt--;
                 }
             } else {
                 if (!Mem_read_byte(0, &data)) 
                    return;
                 ch = (data >> 8) & 0177;
                 if (((tty_out[ln].in_ptr + 1) & 0xff) == tty_out[ln].out_ptr)
                    return;
                 sim_debug(DEBUG_DETAIL, &dte_dev, "TTY queue %x %d\n", ch, ln);
                 tty_out[ln].buff[tty_out[ln].in_ptr] = ch;
                 tty_out[ln].in_ptr = (tty_out[ln].in_ptr + 1) & 0xff;
                 cnt--;
                 if (cnt) {
                     ch = data & 0177;
                     if (((tty_out[ln].in_ptr + 1) & 0xff) == tty_out[ln].out_ptr)
                        return;
                     sim_debug(DEBUG_DETAIL, &dte_dev, "TTY queue %x %d\n", ch, ln);
                     tty_out[ln].buff[tty_out[ln].in_ptr] = ch;
                     tty_out[ln].in_ptr = (tty_out[ln].in_ptr + 1) & 0xff;
                     cnt--;
                 }
             }
         }
         /* If on CTY Queue output done response */
         if (ln < 0) {
             if (((tty_done.in_ptr + 1) & 0xff) != tty_done.out_ptr) {
                  tty_done.buff[tty_done.in_ptr] = (char)(0 & 0xff);
                  tty_done.in_ptr = (tty_done.in_ptr + 1) & 0xff;
             }
         }
         M[ITS_DTEOUT] = FMASK;
         uptr->STATUS |= DTE_11DN;
         if (uptr->STATUS & DTE_PIE)
             set_interrupt(DTE_DEVNUM, uptr->STATUS);
         sim_debug(DEBUG_DETAIL, &dte_dev, "CTY ITS DTEOUT = %012llo\n", word);
     }
     /* Check for line speed */
     word = M[ITS_DTELSP];
     if ((word & SMASK) == 0) {  /* Ready? */
         M[ITS_DTELSP] = FMASK;
         sim_debug(DEBUG_DETAIL, &dte_dev, "CTY ITS DTELSP = %012llo %012llo\n", word, M[ITS_DTELPR]);
     }
     /* Check if any input for it */
     if ((uptr->STATUS & ITS_ON) != 0) {
        word = M[ITS_DTETYI];
        if ((word & SMASK) != 0) {  /* Ready? */
            if (cty_in.in_ptr != cty_in.out_ptr) {
                ch = cty_in.buff[cty_in.out_ptr];
                cty_in.out_ptr = (cty_in.out_ptr + 1) & 0xff;
                word = (uint64)ch;
                M[ITS_DTETYI] = word;
                /* Tell 10 something is ready */
                uptr->STATUS |= DTE_10DB;
                if (uptr->STATUS & DTE_PIE)
                    set_interrupt(DTE_DEVNUM, uptr->STATUS);
            }
       }
       sim_debug(DEBUG_DETAIL, &dte_dev, "CTY ITS DTETYI = %012llo\n", word);
    }
#if 0
     /* Check for input */
     word = M[ITS_DTETYI];
     if ((word & SMASK) != 0) {
        int   l = uptr->CNT;
        do {
           if (tty_connect[l]) {
//         if ((ch = dte_unit[1].CHHOLD) != 0) {
//             word = ch;
//             dte_unit[1].CHHOLD = 0;
//             M[ITS_DTETYI] = word;
//             /* Tell 10 something is ready */
//             uptr->STATUS |= DTE_10DB;
//             if (uptr->STATUS & DTE_PIE)
//                 set_interrupt(DTE_DEVNUM, uptr->STATUS);
//         }
         sim_debug(DEBUG_DETAIL, &dte_dev, "CTY ITS DTETYI = %012llo\n", word);
     }
#endif
     /* Check for output done */
     word = M[ITS_DTEODN];
     if ((word & SMASK) != 0) {
         if (tty_done.in_ptr != tty_done.out_ptr) {
              ln = tty_done.buff[tty_done.out_ptr];
              tty_done.out_ptr = (tty_done.out_ptr + 1) & 0xff;
              word = M[ITS_DTEODN] = (((uint64)ln) << 18)|1;
              sim_debug(DEBUG_DETAIL, &dte_dev, "CTY ITS DTEODN = %012llo\n", word);
              /* Tell 10 something is ready */
              uptr->STATUS |= DTE_10DB;
              if (uptr->STATUS & DTE_PIE)
                  set_interrupt(DTE_DEVNUM, uptr->STATUS);
         }
     }
     /* Check for hangup */
     word = M[ITS_DTEHNG];
     if ((word & SMASK) == 0) {
         sim_debug(DEBUG_DETAIL, &dte_dev, "CTY ITS DTEHNG = %012llo\n", word);
     }
}
#endif

/* Handle primary protocol */
void dte_primary(UNIT *uptr) {
    uint64   word, iword;
    int      s;
    int      cnt;
    struct   _dte_queue *in;
    uint16   data1, data2, *dp;
    
    if ((uptr->STATUS & DTE_11DB) == 0)
        return;

    /* Check if there is room for another packet */
    if (((dte_in_ptr + 1) & 0x1f) == dte_in_cmd) {
        /* If not reschedule ourselves */
        sim_activate(uptr, 100);
        return;
    }
    uptr->STATUS &= ~(DTE_11DB);
    clr_interrupt(DTE_DEVNUM);
    /* Check status word to see if valid */
    if (Mem_examine_word(0, dte_et11_off + PRI_CMTW_STS, &word)) {
error:
         /* If we can't read it, go back to secondary */
         uptr->STATUS |= DTE_SEC;
         return;
    }
    sim_debug(DEBUG_EXP, &dte_dev, "DTE: Read status: %012llo\n", word);

    if ((word & PRI_CMT_QP) == 0) {
         uptr->STATUS |= DTE_SEC;
    }
    in = &dte_in[dte_in_ptr];
    /* Check if indirect */
    if ((word & PRI_CMT_IP) != 0) {
        /* Transfer from 10 */
        if ((uptr->STATUS & DTE_IND) == 0) {
            fprintf(stderr, "DTE out of sync\n\r");
            return;
        }
      word = M[0140 + eb_ptr];
      sim_debug(DEBUG_EXP, &dte_dev, "DTE: Read pointer: %012llo\n", word);
      word = M[0141 + eb_ptr];
      sim_debug(DEBUG_EXP, &dte_dev, "DTE: write pointer: %012llo\n", word);
        /* Get size of transfer */
        if (Mem_examine_word(0, dte_et11_off + PRI_CMTW_CNT, &iword)) 
            goto error;
      sim_debug(DEBUG_EXP, &dte_dev, "DTE: count: %012llo\n", iword);
        in->dcnt = (uint16)(iword & 0177777);
        /* Read in data */
        dp = &in->data[0]; 
        for (cnt = in->dcnt; cnt >= 0; cnt -=2) {
            /* Read in data */
            if (!Mem_read_byte(0, dp))
               goto error;
            sim_debug(DEBUG_DATA, &dte_dev, "DTE: Read Idata: %06o %03o %03o\n", 
                         *dp, *dp >> 8, *dp & 0377);
            dp++;
        }
        uptr->STATUS &= ~DTE_IND;
        dte_in_ptr = (dte_in_ptr + 1) & 0x1f;
    } else {
        /* Transfer from 10 */
        in->dptr = 0;
        /* Read in count */
        if (!Mem_read_byte(0, &data1))
            goto error;
        in->cnt = data1;
        cnt = in->cnt-2;
        if (!Mem_read_byte(0, &data1))
            goto error;
        in->func = data1;
        cnt -= 2;
        if (!Mem_read_byte(0, &data1))
            goto error;
        in->dev = data1;
        cnt -= 2;
        if (!Mem_read_byte(0, &data1))
            goto error;
        in->spare = data1;
        cnt -= 2;
        sim_debug(DEBUG_DATA, &dte_dev, "DTE: Read CMD: %o %o %o\n",
                          in->cnt, in->func, in->dev);
        dp = &in->data[0]; 
        for (; cnt > 0; cnt -=2) {
            /* Read in data */
            if (!Mem_read_byte(0, dp))
               goto error;
            sim_debug(DEBUG_DATA, &dte_dev, "DTE: Read data: %06o %03o %03o\n",
                          *dp, *dp >> 8, *dp & 0377);
            dp++;
        }
        if (in->func & 0100000) {
            uptr->STATUS |= DTE_IND;
            in->sdev = in->dcnt = in->data[0];
            word |= PRI_CMT_TOT;
            if (Mem_deposit_word(0, dte_dt10_off + PRI_CMTW_STS, &word)) 
                goto error;
        } else {
            dte_in_ptr = (dte_in_ptr + 1) & 0x1f;
        }
    }
done:
    word &= ~PRI_CMT_TOT;
    if (Mem_deposit_word(0, dte_dt10_off + PRI_CMTW_STS, &word)) 
        goto error;
    uptr->STATUS |= DTE_11DN;
    if (uptr->STATUS & DTE_PIE)
        set_interrupt(DTE_DEVNUM, uptr->STATUS);
}

void
dte_function(UNIT *uptr)
{
    uint16    data1[32];
    int32     ch;
    struct _dte_queue *cmd;
    t_stat    r;
    
    
    /* Check if queue is empty */
    while (dte_in_cmd != dte_in_ptr) {
        if (((dte_out_res + 1) & 0x1f) == dte_out_ptr) {
            sim_debug(DEBUG_DATA, &dte_dev, "DTE: func out full %d %d\n",
                          dte_out_res, dte_out_ptr);
            return;
        }
        cmd = &dte_in[dte_in_cmd];
        sim_debug(DEBUG_DATA, &dte_dev, "DTE: func %02o %o %d %d\n", cmd->func & 0377,
                cmd->dev, cmd->dcnt, cmd->dptr );
        switch (cmd->func & 0377) {
        case PRI_EM2EI:            /* Initial message to 11 */
               data1[0] = 0;
               if (dte_queue(uptr, PRI_EM2TI, PRI_EMCTY, 1, data1) == 0)
                   return;
//#if (NUM_DEVS_LP > 0)
//               data1[0] = 140;
//               if (dte_queue(uptr, PRI_EMHLA, PRI_EMLPT, 1, data1) == 0)
//                   return;
//#endif

//        data1[0] = ((ln + 1) << 8) | 32;
 //       (void)dte_queue(uptr, PRI_EMHLA, PRI_EMDL1, 1, data1);
//               if (dte_queue(uptr, PRI_EMAKA, PRI_EMDH1, 0, data1) == 0)
//                   return;
               break;
       
        case PRI_EM2TI:            /* Replay to initial message. */
        case PRI_EMACK:            /* Acknowledge line */
               /* Should never get these */
               break;
       
        case PRI_EMSTR:            /* String data */

               /* Handle printer data */
               if (cmd->dev == PRI_EMLPT) {
                   if (!sim_is_active(&lpt_unit))
                       sim_activate(&lpt_unit, 1000);
                   while (cmd->dptr < cmd->dcnt) {
                       if (((lpt_queue.in_ptr + 1) & 0xff) == lpt_queue.out_ptr)
                          return;
                       ch = (int32)(cmd->data[cmd->dptr >> 1]);
                       if ((cmd->dptr & 1) == 0)
                           ch >>= 8;
                       ch &= 0177;
                       lpt_queue.buff[lpt_queue.in_ptr] = ch;
                       lpt_queue.in_ptr = (lpt_queue.in_ptr + 1) & 0xff;
                       cmd->dptr++;
                   }
                   if (cmd->dptr != cmd->dcnt)
                       return;
                   break;
               }

               /* Handle terminal data */
               if ((cmd->dev & 0377) == PRI_EMDLS) {
                   int   ln = ((cmd->sdev >> 8) & 0377) - 1;
                   if (ln < 0)
                       goto cty;
                   if (ln >= tty_desc.lines)
                       break;
                   while (cmd->dptr < cmd->dcnt) {
                       if (((tty_out[ln].in_ptr + 1) & 0xff) == tty_out[ln].out_ptr)
                          return;
                       ch = (int32)(cmd->data[cmd->dptr >> 1]);
                       if ((cmd->dptr & 1) == 0)
                           ch >>= 8;
                       ch &= 0177;
                       sim_debug(DEBUG_DETAIL, &dte_dev, "TTY queue %x %d\n", ch, ln);
                       tty_out[ln].buff[tty_out[ln].in_ptr] = ch;
                       tty_out[ln].in_ptr = (tty_out[ln].in_ptr + 1) & 0xff;
                       cmd->dptr++;
                   }
                   if (cmd->dptr != cmd->dcnt)
                       return;
                   break;
               }
               /* Fall through */
        case PRI_EMSNA:            /* Send all (ttys) */
               if (cmd->dev != PRI_EMCTY) 
                  break;
cty:
               data1[0] = 0;
               while (cmd->dptr < cmd->dcnt) {
                    if (((cty_out.in_ptr + 1) & 0xff) == cty_out.out_ptr)
                        return;
                    ch = (int32)(cmd->data[cmd->dptr >> 1]);
                    if ((cmd->dptr & 1) == 0)
                        ch >>= 8;
                    ch &= 0177;
                    sim_debug(DEBUG_DETAIL, &dte_dev, "CTY type %x\n", ch);
                    ch = sim_tt_outcvt( ch, TT_GET_MODE(uptr->flags));
                    cty_out.buff[cty_out.in_ptr] = (char)(ch & 0xff);
                    cty_out.in_ptr = (cty_out.in_ptr + 1) & 0xff;
                    cty_data = 1;   /* Let output know it needs to ack this */
                    cmd->dptr++;
               }
               if (cmd->dptr != cmd->dcnt)
                   return;
               break;
       
        case PRI_EMLNC:            /* Line-Char */
               /* Send by DTE only? */
#if 0
               data1[0] = 0;
               while (cmd->dptr < cmd->dcnt) {
                    ch = (int32)(cmd->data[cmd->dptr >> 1]);
                    if ((ch >> 8) == PRI_EMCTY) {
                        ch &= 0177;
                        sim_debug(DEBUG_DETAIL, &dte_dev, "CTY ltype %x\n", ch);
                        ch = sim_tt_outcvt( ch, TT_GET_MODE(uptr->flags));
                        if ((r = sim_putchar_s (ch)) != SCPE_OK) /* Output errors */
                           return;
                        data1[0] = (PRI_EMCTY << 8);
                    }
                    cmd->dptr+=2;
               }
               if (cmd->dptr != cmd->dcnt)
                  return;
               if (dte_queue(uptr, PRI_EMACK, PRI_EMCTY, 1, data1) == 0)
                   return;
#endif
               break;
        case PRI_EMRDS:            /* Request device status */
        case PRI_EMHDS:            /* Here is device status */
        case PRI_EMRDT:            /* Request Date/Time */
        case PRI_EMHDR:            /* Here is date and time */
               break;
        case PRI_EMFLO:            /* Flush output */
               if ((cmd->dev & 0377) == PRI_EMDLS) {
                  int   ln = ((cmd->sdev >> 8) & 0377) - 1;;
                  tty_out[ln].in_ptr = tty_out[ln].out_ptr = 0;
               }
               break;
        case PRI_EMDSC:            /* Dataset connect */
               break;
        case PRI_EMHUD:            /* Hang up dataset */
               if ((cmd->dev & 0377) == PRI_EMDLS) {
                  int   ln = ((cmd->sdev >> 8) & 0377) - 1;
                  TMLN  *lp = &tty_ldsc[ln];
                  tmxr_linemsg (lp, "\r\nLine Hangup\r\n");
                  tmxr_reset_ln(lp);
                  tty_connect[ln] = 0;
               }
               break;
        case PRI_EMXOF:            /* XOFF line */
               if ((cmd->dev & 0377) == PRI_EMDLS) {
                  int   ln = ((cmd->sdev >> 8) & 0377) - 1;
                  tty_ldsc[ln].rcve = 0;
               }
               break;
        case PRI_EMXON:            /* XON line */
               if ((cmd->dev & 0377) == PRI_EMDLS) {
                  int   ln = ((cmd->sdev >> 8) & 0377) - 1;
                  tty_ldsc[ln].rcve = 1;
               }
               break;
        case PRI_EMHLS:            /* Here is line speeds */
               if ((cmd->dev & 0377) == PRI_EMDLS) {
                  int   ln = ((cmd->sdev >> 8) & 0377) - 1;
               }
               break;
        case PRI_EMHLA:            /* Here is line allocation */
        case PRI_EMRBI:            /* Reboot information */
        case PRI_EMAKA:            /* Ack ALL */
        case PRI_EMTDO:            /* Turn device On/Off */
               break;
        case PRI_EMEDR:            /* Enable/Disable line */
               if (cmd->dev == PRI_EMDH1) {
                   /* Zero means enable, no-zero means disable */
                   tty_enable = !cmd->data[0];
                   sim_debug(DEBUG_DETAIL, &dte_dev, "CTY enable %x\n", tty_enable);
                   if (tty_enable) {
                      sim_activate(&tty_unit[0], 1000);
                      sim_activate(&tty_unit[1], 1000);
                   } else {
                      sim_cancel(&tty_unit[0]);
                      sim_cancel(&tty_unit[1]);
                   }
               }
               break;
        case PRI_EMLDR:            /* Load LP RAM */
        case PRI_EMLDV:            /* Load LP VFU */
        default:
               break;
        }
        /* Mark command as finished */
        cmd->cnt = 0;
        dte_in_cmd = (dte_in_cmd + 1) & 0x1F;
    }
}

/*
 * Handle primary protocol,
 * Send to 10 when requested.
 */
void dte_transfer(UNIT *uptr) {
    uint64   word;
    int      s;
    uint16   cnt;
    uint16   scnt;
    struct   _dte_queue *out;
    uint16   data1, data2, *dp;

    /* Check if Queue empty */
    if (dte_out_res == dte_out_ptr)
        return;

    out = &dte_out[dte_out_ptr];
    uptr->STATUS &= ~DTE_TO11;
    clr_interrupt(DTE_DEVNUM);

    /* Compute how much 10 wants us to send */
    scnt = ((uptr->CNT ^ DTE_TO10BC) + 1) & DTE_TO10BC;
    /* Check if indirect */
    if ((uptr->STATUS & DTE_SIND) != 0) {
       /* Transfer indirect */
       cnt = out->dcnt+2;
       dp = &out->data[0];
       if (cnt > scnt)  /* Only send as much as we are allowed */
          cnt = scnt;
       for (; cnt > 0; cnt -= 2) {
           sim_debug(DEBUG_DATA, &dte_dev, "DTE: Send Idata: %06o %03o %03o\n",
                          *dp, *dp >> 8, *dp & 0377);
           if (Mem_write_byte(0, dp) == 0)
              goto error;
       }
       uptr->STATUS &= ~DTE_SIND;
    } else {
        sim_debug(DEBUG_DATA, &dte_dev, "DTE: %d %d send CMD: %o %o %o\n", 
                         dte_out_ptr, dte_out_res, out->cnt, out->func, out->dev);
       /* Get size of packet */
       cnt = out->cnt + out->dcnt;
       /* If it will not fit, request indirect */
       if (cnt > scnt) {  /* If not enough space request indirect */
           out->func |= 0100000;
           cnt = scnt;
       }
       /* Write out header */
       if (!Mem_write_byte(0, &cnt))
          goto error;
       if (!Mem_write_byte(0, &out->func))
          goto error;
       cnt -= 2;
       if (!Mem_write_byte(0, &out->dev))
          goto error;
       cnt -= 2;
       if (!Mem_write_byte(0, &out->spare))
           goto error;
       cnt -= 2;
       if (out->func & 0100000) {
           if (!Mem_write_byte(0, &out->dcnt))
              goto error;
           uptr->STATUS |= DTE_SIND;
           goto done;
       }
       cnt -= 2;
       dp = &out->data[0];
       for (; cnt > 0; cnt -= 2) {
           sim_debug(DEBUG_DATA, &dte_dev, "DTE: Send data: %06o %03o %03o\n",
                          *dp, *dp >> 8, *dp & 0377);
           if (!Mem_write_byte(0, dp))
              goto error;
           dp++;
       }
    }
    out->cnt = 0;
    dte_out_ptr = (dte_out_ptr + 1) & 0x1f;
done:
    uptr->STATUS |= DTE_10DN;
//fprintf(stderr, "Xfer done %06o\n\r", uptr->CNT );
    if (uptr->STATUS & DTE_PIE)
        set_interrupt(DTE_DEVNUM, uptr->STATUS);
error:
    return;
}

void
dte_input()
{
    uint16  data1;
    uint16  dataq[32];
    int     n;
    int     save_ptr;
    char    ch;

    /* Check if CTY done with input */
    if (cty_data && cty_out.in_ptr == cty_out.out_ptr) {
        data1 = 0;
        if (dte_queue(&dte_unit[0], PRI_EMACK, PRI_EMCTY, 1, &data1) == 0)
            return;
        cty_data = 0;
    }
    n = 0;
    save_ptr = cty_in.out_ptr;
    while (cty_in.in_ptr != cty_in.out_ptr && n < 32) {
        ch = cty_in.buff[cty_in.out_ptr];
        cty_in.out_ptr = (cty_in.out_ptr + 1) & 0xff;
        sim_debug(DEBUG_DETAIL, &tty_dev, "CTY recieve %02x\n", ch);
        dataq[n++] = ch;
    } 
    if (n > 0 && dte_queue(&dte_unit[0], PRI_EMLNC, PRI_EMCTY, n, dataq) == 0) {
        /* Restore the input pointer */
        cty_in.out_ptr = save_ptr;
        return;
    }
}

/*
 * Queue up a packet to send to 10.
 */
int
dte_queue(UNIT *uptr, int func, int dev, int dcnt, uint16 *data)
{
    uint64   word;
    uint16   *dp;
    UNIT     *optr = &dte_unit[0];
    struct   _dte_queue *out;

    /* Check if room in queue for this packet. */
    if (((dte_out_res + 1) & 0x1f) == dte_out_ptr) {
        sim_debug(DEBUG_DATA, &dte_dev, "DTE: %d %d out full\n", dte_out_res, dte_out_ptr);
        return 0;
    }
    out = &dte_out[dte_out_res];
    out->cnt = 10;
    out->func = func;
    out->dev = dev;
    out->dcnt = (dcnt-1)*2;
    out->spare = 0;
    sim_debug(DEBUG_DATA, &dte_dev, "DTE: %d %d queue resp: %o %o %o\n", 
                         dte_out_ptr, dte_out_res, out->cnt, out->func, out->dev);
    for (dp = &out->data[0]; dcnt > 0; dcnt--) {
         *dp++ = *data++;
    }
   /* Advance pointer to next function */
   dte_out_res = (dte_out_res + 1) & 0x1f;
   return 1;
}


/*
 * If anything in queue, start a transfer, if one is not already
 * pending.
 */
int
dte_start(UNIT *uptr)
{
    uint64   word;
    int      dcnt;

    /* Check if queue empty */
    if (dte_out_ptr == dte_out_res)
        return 1;

    /* If there is interrupt pending, just return */
    if ((uptr->STATUS & (DTE_IND|DTE_10DB|DTE_11DB)) != 0) 
        return 1;
    if (Mem_examine_word(0, dte_et11_off + PRI_CMTW_STS, &word)) {
error:
         /* If we can't read it, go back to secondary */
         uptr->STATUS |= DTE_SEC|DTE_10ER;
         if (uptr->STATUS & DTE_PIE)
             set_interrupt(DTE_DEVNUM, uptr->STATUS);
         return 0;
    }
    /* If in middle of transfer hold off */
    if (word & PRI_CMT_TOT)
       return 1;
    /* Bump count of messages sent */
    word = (word & ~(PRI_CMT_10IC|PRI_CMT_IP)) | ((word + 0400) & PRI_CMT_10IC);
    if (Mem_deposit_word(0, dte_dt10_off + PRI_CMTW_STS, &word))
        goto error;
    word = (uint64)(dte_out[dte_out_ptr].cnt + dte_out[dte_out_ptr].dcnt);
    if (Mem_deposit_word(0, dte_dt10_off + PRI_CMTW_CNT, &word))
        goto error;
    /* Tell 10 something is ready */
    uptr->STATUS |= DTE_10DB;
    if (uptr->STATUS & DTE_PIE)
        set_interrupt(DTE_DEVNUM, uptr->STATUS);
    return 1;
}


/* Handle TO10 traffic */
t_stat dtei_svc (UNIT *uptr)
{
    int32    ch;
    uint32   base = 0;
    UNIT     *optr = &dte_unit[0];
    uint16   data1;

#if KI_22BIT
#if KL_ITS
    if ((cpu_unit[0].flags & UNIT_ITSPAGE) == 0)
#endif
    base = eb_ptr;
#endif
    sim_clock_coschedule (uptr, tmxr_poll);
#if KL_ITS
    if ((uptr->STATUS & (DTE_SEC|ITS_ON)) == 0) {
#else
    if ((uptr->STATUS & (DTE_SEC)) == 0) {
#endif
        dte_function(uptr);  /* Process queue */
        dte_input(uptr);
        dte_start(optr);
    }

    /* Flush out any pending CTY output */
    while(cty_out.in_ptr != cty_out.out_ptr) {
        ch = cty_out.buff[cty_out.out_ptr];
        if (sim_putchar(ch) != SCPE_OK)
            break;
        cty_out.out_ptr = (cty_out.out_ptr + 1) & 0xff;
            sim_debug(DEBUG_DETAIL, &dte_dev, "CTY outch %x '%c'\n", ch,
                            ((ch > 040 && ch < 0177)? ch: '.'));
    }

    /* If we have room see if any new lines */
    if (((cty_in.in_ptr + 1) & 0xff) != cty_in.out_ptr) {
        ch = sim_poll_kbd ();
        if (ch & SCPE_KFLAG) {
            ch = 0177 & sim_tt_inpcvt(ch, TT_GET_MODE (uptr->flags));
            cty_in.buff[cty_in.in_ptr] =ch & 0377;
            cty_in.in_ptr = (cty_in.in_ptr + 1) & 0xff;
            sim_debug(DEBUG_DETAIL, &dte_dev, "CTY char %x '%c'\n", ch,
                            ((ch > 040 && ch < 0177)? ch: '.'));
        }
    }
#if KL_ITS
    if ((optr->STATUS & (DTE_SEC|ITS_ON)) == (DTE_SEC) &&
#else
    if ((optr->STATUS & DTE_SEC) != 0 &&
#endif
         cty_in.in_ptr != cty_in.out_ptr &&
        (optr->STATUS & DTE_MON) != 0 &&
         M[SEC_DTMTI + base] == 0) {
        ch = cty_in.buff[cty_in.out_ptr];
        cty_in.out_ptr = (cty_in.out_ptr + 1) & 0xff;
        M[SEC_DTF11 + base] = ch;
        M[SEC_DTMTI + base] = FMASK;
        optr->STATUS |= DTE_10DB;
        if (optr->STATUS & DTE_PIE)
           set_interrupt(DTE_DEVNUM, optr->STATUS);
    }
#if KL_ITS
    if (QITS && (optr->STATUS & ITS_ON) != 0) {
        uint64  word = M[ITS_DTETYI];
        if ((word & SMASK) != 0) {
            if (cty_in.in_ptr != cty_in.out_ptr) {
                ch = cty_in.buff[cty_in.out_ptr];
                cty_in.out_ptr = (cty_in.out_ptr + 1) & 0xff;
                word = (uint64)ch;
                M[ITS_DTETYI] = word;
                /* Tell 10 something is ready */
                optr->STATUS |= DTE_10DB;
                if (optr->STATUS & DTE_PIE)
                    set_interrupt(DTE_DEVNUM, optr->STATUS);
                sim_debug(DEBUG_DETAIL, &dte_dev, "CTY ITS DTETYI = %012llo\n", word);
            }
       }
    }
#endif
    return SCPE_OK;
}

t_stat
dtertc_srv(UNIT * uptr)
{
    int32 t;
    UNIT     *optr = &dte_unit[0];

    sim_activate_after(uptr, 1000000/rtc_tps);
    /* Check if clock requested */
    if (uptr->STATUS & SEC_CLK) {
        rtc_tick++;
        if (rtc_wait != 0) {
            rtc_wait--;
        } else {
            UNIT     *optr = &dte_unit[0];
            uint32   base = 0;
#if KI_22BIT
            base = eb_ptr;
#endif
            /* Set timer flag */
            M[SEC_DTCLK + base] = FMASK;
            optr->STATUS |= DTE_10DB;
            set_interrupt(DTE_DEVNUM, optr->STATUS);
            sim_debug(DEBUG_EXP, &dte_dev, "CTY tick %x %x %06o\n",
                          rtc_tick, rtc_wait, optr->STATUS);
        }
    }
#if KL_ITS
    /* Check if Timesharing is running */
    if (QITS) {
        uint64     word;

        word = (M[ITS_DTECHK] + 1) & FMASK;
        if (word == 0) {
            optr->STATUS |= ITS_ON;
            sim_debug(DEBUG_DETAIL, &dte_dev, "CTY ITS ON\n");
        } else if (word >= (15 * 60)) {
            optr->STATUS &= ~ITS_ON;
            word = 15 * 60;
            sim_debug(DEBUG_DETAIL, &dte_dev, "CTY ITS OFF\n");
        }
        M[ITS_DTECHK] = word;
    } else
#endif

    /* Update out keep alive timer if in secondary protocol */
    if ((optr->STATUS & DTE_SEC) == 0) {
        int      addr = 0144 + eb_ptr;
        uint64   word;

        (void)Mem_examine_word(0, dte_et11_off + PRI_CMTW_STS, &word);
//fprintf(stderr, "Timer %06o %012llo\n\r", optr->STATUS, word);
        addr = (M[addr+1] + dte_off + PRI_CMTW_KAC) & RMASK;
        word = M[addr];
        word = (word + 1) & FMASK;
        M[addr] = word;
            sim_debug(DEBUG_EXP, &dte_dev, "CTY keepalive %06o %012llo %06o\n",
                          addr, word, optr->STATUS);
    }

    return SCPE_OK;
}


t_stat dte_reset (DEVICE *dptr)
{
    dte_unit[0].STATUS = DTE_SEC;
    dte_unit[1].STATUS = DTE_SEC;
    dte_unit[1].CHHOLD = 0;
    dte_unit[2].STATUS = 0;
//    dte_in_ptr = dte_in_cmd = dte_out_ptr = dte_out_res = 0;
//    cty_in.in_ptr = 0;
//    cty_in.out_ptr = 0;
//    cty_out.in_ptr = 0;
//    cty_out.out_ptr = 0;
    sim_rtcn_init_unit (&dte_unit[2], dte_unit[2].wait, TMR_RTC);
    sim_activate(&dte_unit[1], 100);
    sim_activate(&dte_unit[2], 100);
    return SCPE_OK;
}

/* Stop operating system */

t_stat dte_stop_os (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    M[CTY_SWITCH] = 1;                                 /* tell OS to stop */
    return SCPE_OK;
}

t_stat tty_set_mode (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    dte_unit[0].flags = (dte_unit[0].flags & ~TT_MODE) | val;
    return SCPE_OK;
}

t_stat dte_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "To stop the cpu use the command:\n\n");
fprintf (st, "    sim> SET CTY STOP\n\n");
fprintf (st, "This will write a 1 to location %03o, causing TOPS10 to stop\n\n", CTY_SWITCH);
fprintf (st, "The additional terminals can be set to one of four modes: UC, 7P, 7B, or 8B.\n\n");
fprintf (st, "  mode  input characters        output characters\n\n");
fprintf (st, "  UC    lower case converted    lower case converted to upper case,\n");
fprintf (st, "        to upper case,          high-order bit cleared,\n");
fprintf (st, "        high-order bit cleared  non-printing characters suppressed\n");
fprintf (st, "  7P    high-order bit cleared  high-order bit cleared,\n");
fprintf (st, "                                non-printing characters suppressed\n");
fprintf (st, "  7B    high-order bit cleared  high-order bit cleared\n");
fprintf (st, "  8B    no changes              no changes\n\n");
fprintf (st, "The default mode is 7P.  In addition, each line can be configured to\n");
fprintf (st, "behave as though it was attached to a dataset, or hardwired to a terminal:\n\n");
fprint_reg_help (st, &dte_dev);
return SCPE_OK;
}

const char *dte_description (DEVICE *dptr)
{
    return "Console TTY Line";
}


#if (NUM_DEVS_LP > 0)

void
lpt_printline(UNIT *uptr, int nl) {
    int     trim = 0;
    uint16  data1 = 1;
    /* Trim off trailing blanks */
    while (uptr->COL >= 0 && lpt_buffer[uptr->POS - 1] == ' ') {
         uptr->COL--;
         uptr->POS--;
         trim = 1;
    }
    lpt_buffer[uptr->POS] = '\0';
    sim_debug(DEBUG_DETAIL, &lpt_dev, "LP output %d %d [%s]\n", uptr->COL, nl, lpt_buffer);
    /* Stick a carraige return and linefeed as needed */
    if (uptr->COL != 0 || trim)
        lpt_buffer[uptr->POS++] = '\r';
    if (nl != 0) {
        lpt_buffer[uptr->POS++] = '\n';
        uptr->LINE++;
    }
    if (nl > 0 && uptr->LINE >= ((int32)uptr->capac - MARGIN)) {
        lpt_buffer[uptr->POS++] = '\f';
        uptr->LINE = 0;
    } else if (nl < 0 && uptr->LINE >= (int32)uptr->capac) {
        uptr->LINE = 0;
    }
       
    sim_fwrite(&lpt_buffer, 1, uptr->POS, uptr->fileref);
    uptr->pos += uptr->POS;
    uptr->COL = 0;
    uptr->POS = 0;
//    if (uptr->LINE == 0)
 //      (void)dte_queue(&dte_unit[0], PRI_EMHDS, PRI_EMLPT, 1, &data1);
    return;
}


/* Unit service */
void
lpt_output(UNIT *uptr, char c) {

    if (c == 0)
       return;
    if (uptr->COL == 132)
        lpt_printline(uptr, 1);
    if ((uptr->flags & UNIT_UC) && (c & 0140) == 0140)
        c &= 0137;
    else if (c >= 040 && c < 0177) {
        lpt_buffer[uptr->POS++] = c;
        uptr->COL++;
    }
    return;
}

t_stat lpt_svc (UNIT *uptr)
{
    char    c;
    int     pos;
    int     cpos;
    uint16  data1 = 0;

    if ((uptr->flags & UNIT_ATT) == 0)
        return SCPE_OK;

    while (((lpt_queue.out_ptr + 1) & 0xff) != lpt_queue.in_ptr) {
        c = lpt_queue.buff[lpt_queue.out_ptr];
        lpt_queue.out_ptr = (lpt_queue.out_ptr + 1) & 0xff;
        if (c < 040) { /* Control character */
            switch(c) {
            case 011:     /* Horizontal tab, space to 8'th column */
                      lpt_output(uptr, ' ');
                      while ((uptr->COL & 07) != 0)
                         lpt_output(uptr, ' ');
                      break;
            case 015:     /* Carriage return, print line */
                      lpt_printline(uptr, 0);
                      break;
            case 012:     /* Line feed, print line, space one line */
                      lpt_printline(uptr, 1);
                      break;
            case 014:     /* Form feed, skip to top of page */
                      lpt_printline(uptr, 0);
                      sim_fwrite("\014", 1, 1, uptr->fileref);
                      uptr->pos++;
                      uptr->LINE = 0;
                      break;
            case 013:     /* Vertical tab, Skip mod 20 */
                      lpt_printline(uptr, 1);
                      while((uptr->LINE % 20) != 0) {
                          sim_fwrite("\r\n", 1, 2, uptr->fileref);
                          uptr->pos+=2;
                          uptr->LINE++;
                      }
                      break;
            case 020:     /* Skip half page */
                      lpt_printline(uptr, 1);
                      while((uptr->LINE % 30) != 0) {
                          sim_fwrite("\r\n", 1, 2, uptr->fileref);
                          uptr->pos+=2;
                          uptr->LINE++;
                      }
                      break;
            case 021:     /* Skip even lines */
                      lpt_printline(uptr, 1);
                      while((uptr->LINE % 2) != 0) {
                          sim_fwrite("\r\n", 1, 2, uptr->fileref);
                          uptr->pos+=2;
                          uptr->LINE++;
                      }
                      break;
            case 022:     /* Skip triple lines */
                      lpt_printline(uptr, 1);
                      while((uptr->LINE % 3) != 0) {
                          sim_fwrite("\r\n", 1, 2, uptr->fileref);
                          uptr->pos+=2;
                          uptr->LINE++;
                      }
                      break;
            case 023:     /* Skip one line */
                      lpt_printline(uptr, -1);
                      break;
            default:      /* Ignore */
                      break;
            }
        } else {
            sim_debug(DEBUG_DETAIL, &lpt_dev, "LP deque %02x '%c'\n", c, c);
            lpt_output(uptr, c);
        }
    }
    if (dte_queue(&dte_unit[0], PRI_EMACK, PRI_EMLPT, 1, &data1) == 0)
        sim_activate(uptr, 1000);
    return SCPE_OK;
}

/* Reset routine */

t_stat lpt_reset (DEVICE *dptr)
{
    UNIT *uptr = &lpt_unit;
    uptr->POS = 0;
    uptr->COL = 0;
    uptr->LINE = 1;
    sim_cancel (&lpt_unit);                                 /* deactivate unit */
    return SCPE_OK;
}

/* Attach routine */

t_stat lpt_attach (UNIT *uptr, CONST char *cptr)
{
    t_stat reason;

    return attach_unit (uptr, cptr);
}

/* Detach routine */

t_stat lpt_detach (UNIT *uptr)
{
    return detach_unit (uptr);
}

/*
 * Line printer routines
 */

t_stat
lpt_setlpp(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    t_value   i;
    t_stat    r;
    if (cptr == NULL)
        return SCPE_ARG;
    if (uptr == NULL)
        return SCPE_IERR;
    i = get_uint (cptr, 10, 100, &r);
    if (r != SCPE_OK)
        return SCPE_ARG;
    uptr->capac = (t_addr)i;
    uptr->LINE = 0;
    return SCPE_OK;
}

t_stat
lpt_getlpp(FILE *st, UNIT *uptr, int32 v, CONST void *desc)
{
    if (uptr == NULL)
        return SCPE_IERR;
    fprintf(st, "linesperpage=%d", uptr->capac);
    return SCPE_OK;
}

t_stat lpt_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "Line Printer (LPT)\n\n");
fprintf (st, "The line printer (LPT) writes data to a disk file.  The POS register specifies\n");
fprintf (st, "the number of the next data item to be written.  Thus, by changing POS, the\n");
fprintf (st, "user can backspace or advance the printer.\n");
fprintf (st, "The Line printer can be configured to any number of lines per page with the:\n");
fprintf (st, "        sim> SET %s0 LINESPERPAGE=n\n\n", dptr->name);
fprintf (st, "The default is 66 lines per page.\n\n");
fprintf (st, "The device address of the Line printer can be changed\n");
fprintf (st, "        sim> SET %s0 DEV=n\n\n", dptr->name);
fprint_set_help (st, dptr);
fprint_show_help (st, dptr);
fprint_reg_help (st, dptr);
return SCPE_OK;
}

const char *lpt_description (DEVICE *dptr)
{
    return "LPT0 line printer" ;
}

#endif

#if (NUM_DEVS_TTY > 0)

/* Unit service */
t_stat ttyi_svc (UNIT *uptr)
{
    int32    ln;
    TMLN     *lp;
    int      flg;

    if ((uptr->flags & UNIT_ATT) == 0)                  /* attached? */
        return SCPE_OK;

    sim_clock_coschedule(uptr, tmxr_poll);              /* continue poll */

    /* If we have room see if any new lines */
    if (((tty_hang.in_ptr + 1) & 0xff) != tty_hang.out_ptr) {
        ln = tmxr_poll_conn (&tty_desc);                    /* look for connect */
        if (ln >= 0) {                                      /* got one? rcv enb*/
            tty_hang.buff[tty_hang.in_ptr] = ln + 1;
            tty_hang.in_ptr = (tty_hang.in_ptr + 1) & 0xff;
            tty_connect[ln] = 1;
            sim_debug(DEBUG_DETAIL, &tty_dev, "TTY line connect %d\n", ln);
        }
    }
    
    tmxr_poll_tx(&tty_desc);
    tmxr_poll_rx(&tty_desc);

    /* Scan each line for input */
    for (ln = 0; ln < tty_desc.lines; ln++) {
        lp = &tty_ldsc[ln];
        flg = 1;
        /* Spool up as much as we have room for */
        while (flg && ((tty_out[ln].in_ptr + 1) & 0xff) != tty_out[ln].out_ptr) {
            int32 ch = tmxr_getc_ln(lp);
            if ((ch & TMXR_VALID) != 0) {
                ch = sim_tt_inpcvt (ch, TT_GET_MODE(tty_unit[0].flags) | TTUF_KSR);
                tty_in[ln].buff[tty_in[ln].in_ptr] = ch & 0377;
                tty_in[ln].in_ptr = (tty_in[ln].in_ptr + 1) & 0xff;
                sim_debug(DEBUG_DETAIL, &tty_dev, "TTY recieve %d: %02x\n", ln, ch);
            } else
                flg = 0;
        }
        /* Look for lines that have been disconnected */
        if (tty_connect[ln] == 1 && lp->conn == 0) {
            if (((tty_hang.in_ptr + 1) & 0xff) != tty_hang.out_ptr) {
                tty_hang.buff[tty_hang.in_ptr] = ln + 1;
                tty_hang.in_ptr = (tty_hang.in_ptr + 1) & 0xff;
                tty_connect[ln] = 0;
                sim_debug(DEBUG_DETAIL, &tty_dev, "TTY line disconnect %d\n", ln);
            }
        }
    } 

    return SCPE_OK;
}

/* Output whatever we can */
t_stat ttyo_svc (UNIT *uptr)
{
    t_stat   r;
    int32    ln;
    int      n = 0;
    TMLN     *lp;
    uint16   data1[32];

    if ((tty_unit[0].flags & UNIT_ATT) == 0)                  /* attached? */
        return SCPE_OK;

    sim_clock_coschedule(uptr, tmxr_poll);              /* continue poll */

    for (ln = 0; ln < tty_desc.lines; ln++) {
       lp = &tty_ldsc[ln];
       if (lp->conn == 0)
           continue;
       if (((tty_done.in_ptr + 1) & 0xff) == tty_done.out_ptr)
           return SCPE_OK;
       if (tty_out[ln].out_ptr == tty_out[ln].in_ptr) 
           continue;
       while (tty_out[ln].out_ptr != tty_out[ln].in_ptr) {
           int32 ch = tty_out[ln].buff[tty_out[ln].out_ptr];
           ch = sim_tt_outcvt(ch, TT_GET_MODE (tty_unit[0].flags) | TTUF_KSR);
           sim_debug(DEBUG_DATA, &tty_dev, "TTY: %d output %02x\n", ln, ch);
           r = tmxr_putc_ln (lp, ch);
           if (r == SCPE_OK)
               tty_out[ln].out_ptr = (tty_out[ln].out_ptr + 1) & 0xff;
           else if (r == SCPE_LOST) {
               tty_out[ln].out_ptr = tty_out[ln].in_ptr = 0;
               continue;
           } else
               continue;
       }
       tty_done.buff[tty_done.in_ptr] = ln + 1;
       tty_done.in_ptr = (tty_done.in_ptr + 1) & 0xff;
#if KL_ITS
       /* Tell 10 we have something for it */
       if (QITS) {
           dte_unit[0].STATUS |= DTE_10DB;
           if (dte_unit[0].STATUS & DTE_PIE)
               set_interrupt(DTE_DEVNUM, dte_unit[0].STATUS);
       } 
#endif
    }
    return SCPE_OK;
}

/* Reset routine */

t_stat tty_reset (DEVICE *dptr)
{
    return SCPE_OK;
}


/* SET LINES processor */

t_stat tty_setnl (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    int32 newln, i, t;
    t_stat r;

    if (cptr == NULL)
        return SCPE_ARG;
    newln = (int32) get_uint (cptr, 10, NUM_LINES_TTY, &r);
    if ((r != SCPE_OK) || (newln == tty_desc.lines))
        return r;
    if ((newln == 0) || (newln >= NUM_LINES_TTY) || (newln % 16) != 0)
        return SCPE_ARG;
    if (newln < tty_desc.lines) {
        for (i = newln, t = 0; i < tty_desc.lines; i++)
            t = t | tty_ldsc[i].conn;
        if (t && !get_yn ("This will disconnect users; proceed [N]?", FALSE))
            return SCPE_OK;
        for (i = newln; i < tty_desc.lines; i++) {
            if (tty_ldsc[i].conn) {
                tmxr_linemsg (&tty_ldsc[i], "\r\nOperator disconnected line\r\n");
                tmxr_send_buffered_data (&tty_ldsc[i]);
                }
            tmxr_detach_ln (&tty_ldsc[i]);               /* completely reset line */
        }
    }
    if (tty_desc.lines < newln)
        memset (tty_ldsc + tty_desc.lines, 0, sizeof(*tty_ldsc)*(newln-tty_desc.lines));
    tty_desc.lines = newln;
    return tty_reset (&tty_dev);                         /* setup lines and auto config */
}

/* SET LOG processor */

t_stat tty_set_log (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    t_stat r;
    char gbuf[CBUFSIZE];
    int32 ln;

    if (cptr == NULL)
        return SCPE_ARG;
    cptr = get_glyph (cptr, gbuf, '=');
    if ((cptr == NULL) || (*cptr == 0) || (gbuf[0] == 0))
        return SCPE_ARG;
    ln = (int32) get_uint (gbuf, 10, tty_desc.lines, &r);
    if ((r != SCPE_OK) || (ln >= tty_desc.lines))
        return SCPE_ARG;
    return tmxr_set_log (NULL, ln, cptr, desc);
}

/* SET NOLOG processor */

t_stat tty_set_nolog (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    t_stat r;
    int32 ln;

    if (cptr == NULL)
        return SCPE_ARG;
    ln = (int32) get_uint (cptr, 10, tty_desc.lines, &r);
    if ((r != SCPE_OK) || (ln >= tty_desc.lines))
        return SCPE_ARG;
    return tmxr_set_nolog (NULL, ln, NULL, desc);
}

/* SHOW LOG processor */

t_stat tty_show_log (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    int32 i;

    for (i = 0; i < tty_desc.lines; i++) {
        fprintf (st, "line %d: ", i);
        tmxr_show_log (st, NULL, i, desc);
        fprintf (st, "\n");
        }
    return SCPE_OK;
}


/* Attach routine */

t_stat tty_attach (UNIT *uptr, CONST char *cptr)
{
t_stat reason;

reason = tmxr_attach (&tty_desc, uptr, cptr);
if (reason != SCPE_OK)
  return reason;
sim_activate (uptr, tmxr_poll);
return SCPE_OK;
}

/* Detach routine */

t_stat tty_detach (UNIT *uptr)
{
  int32  i;
  t_stat reason;
reason = tmxr_detach (&tty_desc, uptr);
for (i = 0; i < tty_desc.lines; i++)
    tty_ldsc[i].rcve = 0;
sim_cancel (uptr);
return reason;
}

t_stat tty_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "DC10E Terminal Interfaces\n\n");
fprintf (st, "The DC10 supported up to 8 blocks of 8 lines. Modem control was on a seperate\n");
fprintf (st, "line. The simulator supports this by setting modem control to a fixed offset\n");
fprintf (st, "from the given line. The number of lines is specified with a SET command:\n\n");
fprintf (st, "   sim> SET DC LINES=n          set number of additional lines to n [8-32]\n\n");
fprintf (st, "Lines must be set in multiples of 8.\n");
fprintf (st, "The default offset for modem lines is 32. This can be changed with\n\n");
fprintf (st, "   sim> SET DC MODEM=n          set offset for modem control to n [8-32]\n\n");
fprintf (st, "Modem control must be set larger then the number of lines\n");
fprintf (st, "The ATTACH command specifies the port to be used:\n\n");
tmxr_attach_help (st, dptr, uptr, flag, cptr);
fprintf (st, "The additional terminals can be set to one of four modes: UC, 7P, 7B, or 8B.\n\n");
fprintf (st, "  mode  input characters        output characters\n\n");
fprintf (st, "  UC    lower case converted    lower case converted to upper case,\n");
fprintf (st, "        to upper case,          high-order bit cleared,\n");
fprintf (st, "        high-order bit cleared  non-printing characters suppressed\n");
fprintf (st, "  7P    high-order bit cleared  high-order bit cleared,\n");
fprintf (st, "                                non-printing characters suppressed\n");
fprintf (st, "  7B    high-order bit cleared  high-order bit cleared\n");
fprintf (st, "  8B    no changes              no changes\n\n");
fprintf (st, "The default mode is 7P.\n");
fprintf (st, "Finally, each line supports output logging.  The SET DCn LOG command enables\n");
fprintf (st, "logging on a line:\n\n");
fprintf (st, "   sim> SET DCn LOG=filename   log output of line n to filename\n\n");
fprintf (st, "The SET DCn NOLOG command disables logging and closes the open log file,\n");
fprintf (st, "if any.\n\n");
fprintf (st, "Once DC is attached and the simulator is running, the terminals listen for\n");
fprintf (st, "connections on the specified port.  They assume that the incoming connections\n");
fprintf (st, "are Telnet connections.  The connections remain open until disconnected either\n");
fprintf (st, "by the Telnet client, a SET DC DISCONNECT command, or a DETACH DC command.\n\n");
fprintf (st, "Other special commands:\n\n");
fprintf (st, "   sim> SHOW DC CONNECTIONS    show current connections\n");
fprintf (st, "   sim> SHOW DC STATISTICS     show statistics for active connections\n");
fprintf (st, "   sim> SET DCn DISCONNECT     disconnects the specified line.\n");
fprint_reg_help (st, &tty_dev);
fprintf (st, "\nThe additional terminals do not support save and restore.  All open connections\n");
fprintf (st, "are lost when the simulator shuts down or DC is detached.\n");
return SCPE_OK;
}

const char *tty_description (DEVICE *dptr)
{
return "DC10E asynchronous line interface";
}

#endif
#endif