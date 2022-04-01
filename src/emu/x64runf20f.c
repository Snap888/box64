#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>

#include "debug.h"
#include "box64stack.h"
#include "x64emu.h"
#include "x64run.h"
#include "x64emu_private.h"
#include "x64run_private.h"
#include "x64primop.h"
#include "x64trace.h"
#include "x87emu_private.h"
#include "box64context.h"
#include "bridge.h"

#include "modrm.h"

int RunF20F(x64emu_t *emu, rex_t rex)
{
    uint8_t opcode;
    uint8_t nextop;
    int8_t tmp8s;
    uint8_t tmp8u;
    int32_t tmp32s;
    reg64_t *oped, *opgd;
    sse_regs_t *opex, *opgx, eax1;
    mmx87_regs_t *opgm;

    opcode = F8;

    switch(opcode) {

    case 0x10:  /* MOVSD Gx, Ex */
        nextop = F8;
        GETEX(0);
        GETGX;
        GX->q[0] = EX->q[0];
        if((nextop&0xC0)!=0xC0) {
            // EX is not a register
            GX->q[1] = 0;
        }
        break;
    case 0x11:  /* MOVSD Ex, Gx */
        nextop = F8;
        GETEX(0);
        GETGX;
        EX->q[0] = GX->q[0];
        break;
    case 0x12:  /* MOVDDUP Gx, Ex */
        nextop = F8;
        GETEX(0);
        GETGX;
        GX->q[1] = GX->q[0] = EX->q[0];
        break;

    case 0x2A:  /* CVTSI2SD Gx, Ed */
        nextop = F8;
        GETED(0);
        GETGX;
        if(rex.w) {
            GX->d[0] = ED->sq[0];
        } else {
            GX->d[0] = ED->sdword[0];
        }
        break;

    case 0x2C:  /* CVTTSD2SI Gd, Ex */
        nextop = F8;
        GETEX(0);
        GETGD;
        if(rex.w)
            GD->sq[0] = EX->d[0];
        else {
            GD->sdword[0] = EX->d[0];
            GD->dword[1] = 0;
        }
        break;
    case 0x2D:  /* CVTSD2SI Gd, Ex */
        nextop = F8;
        GETEX(0);
        GETGD;
        if(rex.w) {
            switch((emu->mxcsr>>13)&3) {
                case ROUND_Nearest:
                    GD->q[0] = floor(EX->d[0]+0.5);
                    break;
                case ROUND_Down:
                    GD->q[0] = floor(EX->d[0]);
                    break;
                case ROUND_Up:
                    GD->q[0] = ceil(EX->d[0]);
                    break;
                case ROUND_Chop:
                    GD->q[0] = EX->d[0];
                    break;
            }
        } else {
            switch((emu->mxcsr>>13)&3) {
                case ROUND_Nearest:
                    GD->sdword[0] = floor(EX->d[0]+0.5);
                    break;
                case ROUND_Down:
                    GD->sdword[0] = floor(EX->d[0]);
                    break;
                case ROUND_Up:
                    GD->sdword[0] = ceil(EX->d[0]);
                    break;
                case ROUND_Chop:
                    GD->sdword[0] = EX->d[0];
                    break;
            }
            GD->dword[1] = 0;
        }
        break;
        
    case 0x51:  /* SQRTSD Gx, Ex */
        nextop = F8;
        GETEX(0);
        GETGX;
        if(EX->d[0]<0.0 )
            GX->d[0] = -NAN;
        else
            GX->d[0] = sqrt(EX->d[0]);
        break;

    case 0x58:  /* ADDSD Gx, Ex */
        nextop = F8;
        GETEX(0);
        GETGX;
        GX->d[0] += EX->d[0];
        break;
    case 0x59:  /* MULSD Gx, Ex */
        nextop = F8;
        GETEX(0);
        GETGX;
        #ifndef NOALIGN
            // mul generate a -NAN only if doing (+/-)inf * (+/-)0
            if((isinf(GX->d[0]) && EX->d[0]==0.0) || (isinf(EX->d[0]) && GX->d[0]==0.0))
                GX->d[0] = -NAN;
            else
        #endif
        GX->d[0] *= EX->d[0];
        break;
    case 0x5A:  /* CVTSD2SS Gx, Ex */
        nextop = F8;
        GETEX(0);
        GETGX;
        GX->f[0] = EX->d[0];
        break;

    case 0x5C:  /* SUBSD Gx, Ex */
        nextop = F8;
        GETEX(0);
        GETGX;
        GX->d[0] -= EX->d[0];
        break;
    case 0x5D:  /* MINSD Gx, Ex */
        nextop = F8;
        GETEX(0);
        GETGX;
        if (isnan(GX->d[0]) || isnan(EX->d[0]) || isless(EX->d[0], GX->d[0]))
            GX->d[0] = EX->d[0];
        break;
    case 0x5E:  /* DIVSD Gx, Ex */
        nextop = F8;
        GETEX(0);
        GETGX;
        GX->d[0] /= EX->d[0];
        break;
    case 0x5F:  /* MAXSD Gx, Ex */
        nextop = F8;
        GETEX(0);
        GETGX;
        if (isnan(GX->d[0]) || isnan(EX->d[0]) || isgreater(EX->d[0], GX->d[0]))
            GX->d[0] = EX->d[0];
        break;

    case 0x70:  /* PSHUFLW Gx, Ex, Ib */
        nextop = F8;
        GETEX(1);
        GETGX;
        tmp8u = F8;
        if(GX==EX) {
            for (int i=0; i<4; ++i)
                eax1.uw[i] = EX->uw[(tmp8u>>(i*2))&3];
            GX->q[0] = eax1.q[0];
        } else {
            for (int i=0; i<4; ++i)
                GX->uw[i] = EX->uw[(tmp8u>>(i*2))&3];
            GX->q[1] = EX->q[1];
        }
        break;

    case 0x7C:  /* HADDPS Gx, Ex */
        nextop = F8;
        GETEX(0);
        GETGX;
        GX->f[0] += GX->f[1];
        GX->f[1] = GX->f[2] + GX->f[3];
        if(EX==GX) {
            GX->f[2] = GX->f[0];
            GX->f[3] = GX->f[1];
        } else {
            GX->f[2] = EX->f[0] + EX->f[1];
            GX->f[3] = EX->f[2] + EX->f[3];
        }
        break;

    GOCOND(0x80
        , tmp32s = F32S; CHECK_FLAGS(emu);
        , R_RIP += tmp32s;
        ,
    )                               /* 0x80 -> 0x8F Jxx */
        
    case 0xC2:  /* CMPSD Gx, Ex, Ib */
        nextop = F8;
        GETEX(1);
        GETGX;
        tmp8u = F8;
        tmp8s = 0;
        switch(tmp8u&7) {
            case 0: tmp8s=(GX->d[0] == EX->d[0]); break;
            case 1: tmp8s=isless(GX->d[0], EX->d[0]) && !(isnan(GX->d[0]) || isnan(EX->d[0])); break;
            case 2: tmp8s=islessequal(GX->d[0], EX->d[0]) && !(isnan(GX->d[0]) || isnan(EX->d[0])); break;
            case 3: tmp8s=isnan(GX->d[0]) || isnan(EX->d[0]); break;
            case 4: tmp8s=isnan(GX->d[0]) || isnan(EX->d[0]) || (GX->d[0] != EX->d[0]); break;
            case 5: tmp8s=isnan(GX->d[0]) || isnan(EX->d[0]) || isgreaterequal(GX->d[0], EX->d[0]); break;
            case 6: tmp8s=isnan(GX->d[0]) || isnan(EX->d[0]) || isgreater(GX->d[0], EX->d[0]); break;
            case 7: tmp8s=!isnan(GX->d[0]) && !isnan(EX->d[0]); break;
        }
        GX->q[0]=(tmp8s)?0xffffffffffffffffLL:0LL;
        break;

    case 0xD0:  /* ADDSUBPS Gx, Ex */
        nextop = F8;
        GETEX(0);
        GETGX;
        GX->f[0] -= EX->f[0];
        GX->f[1] += EX->f[1];
        GX->f[2] -= EX->f[2];
        GX->f[3] += EX->f[3];
        break;

    case 0xD6:  /* MOVDQ2Q Gm, Ex */
        nextop = F8;
        GETEX(0);
        GETGM;
        GM->q = EX->q[0];
        break;

    case 0xE6:  /* CVTPD2DQ Gx, Ex */
        nextop = F8;
        GETEX(0);
        GETGX;
        switch((emu->mxcsr>>13)&3) {
            case ROUND_Nearest:
                GX->sd[0] = floor(EX->d[0]+0.5);
                GX->sd[1] = floor(EX->d[1]+0.5);
                break;
            case ROUND_Down:
                GX->sd[0] = floor(EX->d[0]);
                GX->sd[1] = floor(EX->d[1]);
                break;
            case ROUND_Up:
                GX->sd[0] = ceil(EX->d[0]);
                GX->sd[1] = ceil(EX->d[1]);
                break;
            case ROUND_Chop:
                GX->sd[0] = EX->d[0];
                GX->sd[1] = EX->d[1];
                break;
        }
        GX->q[1] = 0;
        break;

    case 0xF0:  /* LDDQU Gx, Ex */
        nextop = F8;
        GETEX(0);
        GETGX;
        memcpy(GX, EX, 16);
        break;

   default:
        return 1;
    }
    return 0;
}