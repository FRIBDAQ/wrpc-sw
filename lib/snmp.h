/*
 * This work is part of the White Rabbit project
 *
 * Copyright (C) 2016 GSI (www.gsi.de), CERN (www.cern.ch)
 * Author: Alessandro Rubini <a.rubini@gsi.de>
 * Author: Adam Wujek <adam.wujek@cern.ch>
 *
 * Released according to the GNU GPL, version 2 or any later version.
 */

#ifndef __SNMP_H
#define __SNMP_H

#define ASN_BOOLEAN	((u_char)0x01)
#define ASN_INTEGER	((u_char)0x02)
#define ASN_OCTET_STR	((u_char)0x04)
#define ASN_NULL	((u_char)0x05)
#define ASN_OBJECT_ID	((u_char)0x06)
#define ASN_APPLICATION	((u_char)0x40)
#define ASN_IPADDRESS	(ASN_APPLICATION | 0)
#define ASN_COUNTER	(ASN_APPLICATION | 1)
#define ASN_GAUGE	(ASN_APPLICATION | 2)
#define ASN_UNSIGNED	(ASN_APPLICATION | 2)   /* RFC 1902 - same as GAUGE */
#define ASN_TIMETICKS	(ASN_APPLICATION | 3)
#define ASN_COUNTER64   (ASN_APPLICATION | 6)
/*
 * (error codes from net-snmp's snmp.h)
 * Error codes (the value of the field error-status in PDUs)
 */
#define SNMP_ERR_NOERROR		(0)
#define SNMP_ERR_TOOBIG			(1)
#define SNMP_ERR_NOSUCHNAME		(2)
#define SNMP_ERR_BADVALUE		(3)
#define SNMP_ERR_READONLY		(4)
#define SNMP_ERR_GENERR			(5)

#define SNMP_GET 0xA0
#define SNMP_GET_NEXT 0xA1
#define SNMP_GET_RESPONSE 0xA2
#define SNMP_SET 0xA3

#define SNMP_V1 0
#define SNMP_V2c 1
#define SNMP_V_MAX 1 /* Maximum supported version */

#ifdef CONFIG_SNMP_SET
#define SNMP_SET_ENABLED 1
#define SNMP_SET_FUNCTION(X) .set = (X)
#else
#define SNMP_SET_ENABLED 0
#define SNMP_SET_FUNCTION(X) .set = NULL
#endif

#ifdef CONFIG_SNMP_AUX_DIAG
#define SNMP_AUX_DIAG_ENABLED 1
#else
#define SNMP_AUX_DIAG_ENABLED 0
#endif

#define MASK_SET 0x1
#define MASK_GET 0x2
#define MASK_GET_NEXT 0x4
#define RETURN_FIRST 0x8
/* used to define write only OIDs */
#define NO_SET NULL

/* limit string len */
#define MAX_OCTET_STR_LEN 32

/* defines used by get_time function */
#define TIME_STRING 1
#define TIME_NUM 0
#define TYPE_MASK 1
#define UPTIME_MASK 2
#define TAI_MASK 4
#define TAI_STRING (void *) (TAI_MASK | TIME_STRING)
#define TAI_NUM (void *) (TAI_MASK | TIME_NUM)
#define UPTIME_NUM (void *) (UPTIME_MASK | TIME_NUM)

#define PTP_SERVO_STATE_N_STANDARD_PTP 99

/* defines used by get_servo function */
#define SERVO_STATEN          (void *) 1
#define SERVO_CLOCKOFFSET     (void *) 2
#define SERVO_SKEW            (void *) 3
#define SERVO_RTT             (void *) 4
#define SERVO_UPDATE_TIME     (void *) 5
#define SERVO_DELTA_TX_M      (void *) 6
#define SERVO_DELTA_RX_M      (void *) 7
#define SERVO_DELTA_TX_S      (void *) 8
#define SERVO_DELTA_RX_S      (void *) 9
#define SERVO_N_ERR_STATE     (void *) 10
#define SERVO_N_ERR_OFFSET    (void *) 11
#define SERVO_N_ERR_DELTA_RTT (void *) 12
#define SERVO_ASYMMETRY       (void *) 13


/* defines used by get_port function */
#define PORT_LINK_STATUS (void *) 1

/* defines for wrpcPtpConfigRestart */
#define restartPtp 1
#define restartPtpSuccessful 100
#define restartPtpFailed 200

/* defines for wrpcPtpConfigApply */
#define writeToFlashGivenSfp 1
#define writeToFlashCurrentSfp 2
#define writeToMemoryCurrentSfp 3
#define eraseFlash 50
#define applySuccessful 100
#define applySuccessfulMatchFailed 101
#define applyFailed 200
#define applyFailedI2CError 201
#define applyFailedDBFull 202
#define applyFailedInvalidPN 203

/* new defines for wrpcInitScriptConfigApply, some are used from
 * wrpcPtpConfigApply */
#define writeToFlash 1
#define applyFailedEmptyLine 201

/* new defines for oid_wrpcSdbApply, some are used from
 * wrpcPtpConfigApply */
#define applyFailedEmptyParam 201

/* new defines for oid_wrpcShellCmdRun */
#define execute 12
#define executionSuccessful 100
#define executionFailed 200
#define executionFailedEmptyLine 201
#define executionFailedSetMagicTo44451 202
#define shellCmdReturnCodeMAGIC 44451

/* defines for wrpcTemperatureTable */
#define TABLE_ROW 1
#define TABLE_COL 0
#define TABLE_ENTRY 0
#define TABLE_FIRST_ROW 1
#define TABLE_FIRST_COL 2

/* Limit community length. Standard says nothing about maximum length, but we
 * want to limit it to save memory */
#define MAX_COMMUNITY_LEN 32
/* Limit the request-id. Standard says max is 4 */
#define MAX_REQID_LEN 4
/* Limit the OID length */
#define MAX_OID_LEN 40

/* Index of a byte in the OID corresponding to the ID of the aux diag
 * registers */
#define AUX_DIAG_ID_INDEX 8
/* Index of a byte in the OID corresponding to the version of the aux diag
 * registers */
#define AUX_DIAG_VER_INDEX 9

#define AUX_DIAG_RO (void *) DIAG_RO_BANK
#define AUX_DIAG_RW (void *) DIAG_RW_BANK
/* add the AUX_DIAG_DATA_COL_SHIFT to the aux diag register's index to get
 * column in the table. Columns are following:
 * col 1 -- index
 * col 2 -- number of registers
 * col 3 -- first aux diag register
 */
#define AUX_DIAG_DATA_COL_SHIFT 3

#define OID_FIELD_STRUCT(_oid, _getf, _setf, _asn, _type, _pointer, _field) { \
	.oid_match = _oid, \
	.oid_len = sizeof(_oid), \
	.get = _getf, \
	SNMP_SET_FUNCTION(_setf), \
	.asn = _asn, \
	.p = _pointer, \
	.offset = offsetof(_type, _field), \
	.data_size = sizeof(((_type *)0)->_field) \
}

#define OID_FIELD(_oid, _fname, _asn) { \
	.oid_match = _oid, \
	.oid_len = sizeof(_oid), \
	.get = _fname, \
	.asn = _asn, \
}

#define OID_FIELD_VAR(_oid, _getf, _setf, _asn, _pointer) { \
	.oid_match = _oid, \
	.oid_len = sizeof(_oid), \
	.get = _getf, \
	SNMP_SET_FUNCTION(_setf), \
	.asn = _asn, \
	.p = _pointer, \
	.offset = 0, \
	.data_size = sizeof(*_pointer) \
}

struct snmp_oid {
	const uint8_t *oid_match;
	int (*get)(uint8_t *buf, struct snmp_oid *obj);
	/* *set is needed only when support for SNMP SET is enabled */
	int (*set)(uint8_t *buf, struct snmp_oid *obj);
	void *p;
	uint8_t oid_len;
	uint8_t asn;
	uint8_t offset; /* increase it if it is not enough */
	uint8_t data_size;
};

#define OID_LIMB_FIELD(_oid, _func, _obj_array) { \
	.oid_match = _oid, \
	.oid_len = sizeof(_oid), \
	.twig_func = _func, \
	.obj_array = _obj_array, \
}

struct snmp_oid_limb {
	const uint8_t *oid_match;
	int (*twig_func)(uint8_t *buf, uint8_t in_oid_limb_matched_len,
			  struct snmp_oid *obj, uint8_t flags);
	struct snmp_oid *obj_array;
	uint8_t oid_len;
};

#endif