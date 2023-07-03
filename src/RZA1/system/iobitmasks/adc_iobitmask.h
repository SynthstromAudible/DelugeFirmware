/*******************************************************************************
* DISCLAIMER
* This software is supplied by Renesas Electronics Corporation and is only
* intended for use with Renesas products. No other uses are authorized. This
* software is owned by Renesas Electronics Corporation and is protected under
* all applicable laws, including copyright laws.
* THIS SOFTWARE IS PROVIDED "AS IS" AND RENESAS MAKES NO WARRANTIES REGARDING
* THIS SOFTWARE, WHETHER EXPRESS, IMPLIED OR STATUTORY, INCLUDING BUT NOT
* LIMITED TO WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE
* AND NON-INFRINGEMENT. ALL SUCH WARRANTIES ARE EXPRESSLY DISCLAIMED.
* TO THE MAXIMUM EXTENT PERMITTED NOT PROHIBITED BY LAW, NEITHER RENESAS
* ELECTRONICS CORPORATION NOR ANY OF ITS AFFILIATED COMPANIES SHALL BE LIABLE
* FOR ANY DIRECT, INDIRECT, SPECIAL, INCIDENTAL OR CONSEQUENTIAL DAMAGES FOR
* ANY REASON RELATED TO THIS SOFTWARE, EVEN IF RENESAS OR ITS AFFILIATES HAVE
* BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGES.
* Renesas reserves the right, without notice, to make changes to this software
* and to discontinue the availability of this software. By using this software,
* you agree to the additional terms and conditions found by accessing the
* following link:
* http://www.renesas.com/disclaimer
*
*******************************************************************************/
/*******************************************************************************
* Copyright (C) 2013 Renesas Electronics Corporation. All rights reserved.
*******************************************************************************/
/*******************************************************************************
* File Name     : adc_iobitmask.h
* Device(s)     : RZ/A1H RSK+RZA1H
* H/W Platform  : RSK+RZA1H CPU Board
* Description   : ADC register define header
*******************************************************************************/
/*******************************************************************************
* History       : DD.MM.YYYY Version Description
*               : 18.06.2013 1.00
*******************************************************************************/

#ifndef ADC_IOBITMASK_H
#define ADC_IOBITMASK_H


#define ADC_ADDRA_D                            (0xFFFFu)

#define ADC_ADDRB_D                            (0xFFFFu)

#define ADC_ADDRC_D                            (0xFFFFu)

#define ADC_ADDRD_D                            (0xFFFFu)

#define ADC_ADDRE_D                            (0xFFFFu)

#define ADC_ADDRF_D                            (0xFFFFu)

#define ADC_ADDRG_D                            (0xFFFFu)

#define ADC_ADDRH_D                            (0xFFFFu)

#define ADC_MPHA_D                             (0xFFFFu)

#define ADC_MPLA_D                             (0xFFFFu)

#define ADC_MPHB_D                             (0xFFFFu)

#define ADC_MPLB_D                             (0xFFFFu)

#define ADC_MPHC_D                             (0xFFFFu)

#define ADC_MPLC_D                             (0xFFFFu)

#define ADC_MPHD_D                             (0xFFFFu)

#define ADC_MPLD_D                             (0xFFFFu)

#define ADC_MPHE_D                             (0xFFFFu)

#define ADC_MPLE_D                             (0xFFFFu)

#define ADC_MPHF_D                             (0xFFFFu)

#define ADC_MPLF_D                             (0xFFFFu)

#define ADC_MPHG_D                             (0xFFFFu)

#define ADC_MPLG_D                             (0xFFFFu)

#define ADC_MPHH_D                             (0xFFFFu)

#define ADC_MPLH_D                             (0xFFFFu)

#define ADC_SR_CH                              (0x0007u)
#define ADC_SR_MDS                             (0x0038u)
#define ADC_SR_CKS                             (0x01C0u)
#define ADC_SR_TRGS                            (0x1E00u)
#define ADC_SR_ADST                            (0x2000u)
#define ADC_SR_ADIE                            (0x4000u)
#define ADC_SR_ADF                             (0x8000u)

#define ADC_MPER_LLMENA                        (0x0001u)
#define ADC_MPER_LLMENB                        (0x0002u)
#define ADC_MPER_LLMENC                        (0x0004u)
#define ADC_MPER_LLMEND                        (0x0008u)
#define ADC_MPER_LLMENE                        (0x0010u)
#define ADC_MPER_LLMENF                        (0x0020u)
#define ADC_MPER_LLMENG                        (0x0040u)
#define ADC_MPER_LLMENH                        (0x0080u)
#define ADC_MPER_HLMENA                        (0x0100u)
#define ADC_MPER_HLMENB                        (0x0200u)
#define ADC_MPER_HLMENC                        (0x0400u)
#define ADC_MPER_HLMEND                        (0x0800u)
#define ADC_MPER_HLMENE                        (0x1000u)
#define ADC_MPER_HLMENF                        (0x2000u)
#define ADC_MPER_HLMENG                        (0x4000u)
#define ADC_MPER_HLMENH                        (0x8000u)

#define ADC_MPSR_LUDRA                         (0x0001u)
#define ADC_MPSR_LUDRB                         (0x0002u)
#define ADC_MPSR_LUDRC                         (0x0004u)
#define ADC_MPSR_LUDRD                         (0x0008u)
#define ADC_MPSR_LUDRE                         (0x0010u)
#define ADC_MPSR_LUDRF                         (0x0020u)
#define ADC_MPSR_LUDRG                         (0x0040u)
#define ADC_MPSR_LUDRH                         (0x0080u)
#define ADC_MPSR_HOVRA                         (0x0100u)
#define ADC_MPSR_HOVRB                         (0x0200u)
#define ADC_MPSR_HOVRC                         (0x0400u)
#define ADC_MPSR_HOVRD                         (0x0800u)
#define ADC_MPSR_HOVRE                         (0x1000u)
#define ADC_MPSR_HOVRF                         (0x2000u)
#define ADC_MPSR_HOVRG                         (0x4000u)
#define ADC_MPSR_HOVRH                         (0x8000u)


#define ADC_ADDRA_D_SHIFT                      (0u)

#define ADC_ADDRB_D_SHIFT                      (0u)

#define ADC_ADDRC_D_SHIFT                      (0u)

#define ADC_ADDRD_D_SHIFT                      (0u)

#define ADC_ADDRE_D_SHIFT                      (0u)

#define ADC_ADDRF_D_SHIFT                      (0u)

#define ADC_ADDRG_D_SHIFT                      (0u)

#define ADC_ADDRH_D_SHIFT                      (0u)

#define ADC_MPHA_D_SHIFT                       (0u)

#define ADC_MPLA_D_SHIFT                       (0u)

#define ADC_MPHB_D_SHIFT                       (0u)

#define ADC_MPLB_D_SHIFT                       (0u)

#define ADC_MPHC_D_SHIFT                       (0u)

#define ADC_MPLC_D_SHIFT                       (0u)

#define ADC_MPHD_D_SHIFT                       (0u)

#define ADC_MPLD_D_SHIFT                       (0u)

#define ADC_MPHE_D_SHIFT                       (0u)

#define ADC_MPLE_D_SHIFT                       (0u)

#define ADC_MPHF_D_SHIFT                       (0u)

#define ADC_MPLF_D_SHIFT                       (0u)

#define ADC_MPHG_D_SHIFT                       (0u)

#define ADC_MPLG_D_SHIFT                       (0u)

#define ADC_MPHH_D_SHIFT                       (0u)

#define ADC_MPLH_D_SHIFT                       (0u)

#define ADC_SR_CH_SHIFT                        (0u)
#define ADC_SR_MDS_SHIFT                       (3u)
#define ADC_SR_CKS_SHIFT                       (6u)
#define ADC_SR_TRGS_SHIFT                      (9u)
#define ADC_SR_ADST_SHIFT                      (13u)
#define ADC_SR_ADIE_SHIFT                      (14u)
#define ADC_SR_ADF_SHIFT                       (15u)

#define ADC_MPER_LLMENA_SHIFT                  (0u)
#define ADC_MPER_LLMENB_SHIFT                  (1u)
#define ADC_MPER_LLMENC_SHIFT                  (2u)
#define ADC_MPER_LLMEND_SHIFT                  (3u)
#define ADC_MPER_LLMENE_SHIFT                  (4u)
#define ADC_MPER_LLMENF_SHIFT                  (5u)
#define ADC_MPER_LLMENG_SHIFT                  (6u)
#define ADC_MPER_LLMENH_SHIFT                  (7u)
#define ADC_MPER_HLMENA_SHIFT                  (8u)
#define ADC_MPER_HLMENB_SHIFT                  (9u)
#define ADC_MPER_HLMENC_SHIFT                  (10u)
#define ADC_MPER_HLMEND_SHIFT                  (11u)
#define ADC_MPER_HLMENE_SHIFT                  (12u)
#define ADC_MPER_HLMENF_SHIFT                  (13u)
#define ADC_MPER_HLMENG_SHIFT                  (14u)
#define ADC_MPER_HLMENH_SHIFT                  (15u)

#define ADC_MPSR_LUDRA_SHIFT                   (0u)
#define ADC_MPSR_LUDRB_SHIFT                   (1u)
#define ADC_MPSR_LUDRC_SHIFT                   (2u)
#define ADC_MPSR_LUDRD_SHIFT                   (3u)
#define ADC_MPSR_LUDRE_SHIFT                   (4u)
#define ADC_MPSR_LUDRF_SHIFT                   (5u)
#define ADC_MPSR_LUDRG_SHIFT                   (6u)
#define ADC_MPSR_LUDRH_SHIFT                   (7u)
#define ADC_MPSR_HOVRA_SHIFT                   (8u)
#define ADC_MPSR_HOVRB_SHIFT                   (9u)
#define ADC_MPSR_HOVRC_SHIFT                   (10u)
#define ADC_MPSR_HOVRD_SHIFT                   (11u)
#define ADC_MPSR_HOVRE_SHIFT                   (12u)
#define ADC_MPSR_HOVRF_SHIFT                   (13u)
#define ADC_MPSR_HOVRG_SHIFT                   (14u)
#define ADC_MPSR_HOVRH_SHIFT                   (15u)


#endif /* ADC_IOBITMASK_H */

/* End of File */
