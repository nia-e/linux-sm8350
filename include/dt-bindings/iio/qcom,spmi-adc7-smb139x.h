/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020 The Linux Foundation. All rights reserved.
 */

// XXX: Clean up later

#ifndef _DT_BINDINGS_QCOM_SPMI_VADC_SMB139X_H
#define _DT_BINDINGS_QCOM_SPMI_VADC_SMB139X_H

#define SMB139x_1_ADC7_SMB_TEMP			(SMB139x_1_SID << 8 | 0x06)
#define SMB139x_1_ADC7_ICHG_SMB			(SMB139x_1_SID << 8 | 0x18)
#define SMB139x_1_ADC7_IIN_SMB			(SMB139x_1_SID << 8 | 0x19)

#define SMB139x_2_ADC7_SMB_TEMP			(SMB139x_2_SID << 8 | 0x06)
#define SMB139x_2_ADC7_ICHG_SMB			(SMB139x_2_SID << 8 | 0x18)
#define SMB139x_2_ADC7_IIN_SMB			(SMB139x_2_SID << 8 | 0x19)

#endif
