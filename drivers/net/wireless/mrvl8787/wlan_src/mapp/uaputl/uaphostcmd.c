/** @file  uaphostcmd.c
  *
  * @brief This file contains uAP hostcmd functions
  * 
  *  Copyright (C) 2010, Marvell International Ltd.
  *  All Rights Reserved
  */
/************************************************************************
Change log:
     11/26/2008: initial version
************************************************************************/

#include    <stdlib.h>
#include    <stdio.h>
#include    <string.h>
#include    <ctype.h>
#include	"uaputl.h"

#ifndef MIN
/** Find minimum value */
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif /* MIN */

/********************************************************
		Local Variables
********************************************************/

/********************************************************
		Global Variables
********************************************************/

/********************************************************
		Local Functions
********************************************************/
/* 
 *  @brief convert String to integer
 *  
 *  @param value	A pointer to string
 *  @return             integer
 */
static t_u32
a2hex_or_atoi(t_s8 * value)
{
    if (value[0] == '0' && (value[1] == 'X' || value[1] == 'x')) {
        return a2hex(value + 2);
    } else if (isdigit(*value)) {
        return atoi(value);
    } else {
        return *value;
    }
}

/** 
 *  @brief Get one line from the File
 *  
 *  @param fp       File handler
 *  @param s	    Storage location for data.
 *  @param size 	Maximum number of characters to read. 
 *  @param line		A pointer to return current line number
 *  @return         returns string or NULL 
 */
static t_s8 *
mlan_config_get_line(FILE * fp, t_s8 * s, t_s32 size, int *line)
{
    t_s8 *pos, *end, *sstart;

    while (fgets(s, size, fp)) {
        (*line)++;
        s[size - 1] = '\0';
        pos = s;

        while (*pos == ' ' || *pos == '\t')
            pos++;
        if (*pos == '#' || (*pos == '\r' && *(pos + 1) == '\n') ||
            *pos == '\n' || *pos == '\0')
            continue;

        /* Remove # comments unless they are within a double quoted * string.
           Remove trailing white space. */
        sstart = strchr(pos, '"');
        if (sstart)
            sstart = strchr(sstart + 1, '"');
        if (!sstart)
            sstart = pos;
        end = strchr(sstart, '#');
        if (end)
            *end-- = '\0';
        else
            end = pos + strlen(pos) - 1;
        while (end > pos && (*end == '\r' || *end == '\n' ||
                             *end == ' ' || *end == '\t')) {
            *end-- = '\0';
        }
        if (*pos == '\0')
            continue;
        return pos;
    }
    return NULL;
}

/** 
 *  @brief get hostcmd data
 *  
 *  @param ln			A pointer to line number
 *  @param buf			A pointer to hostcmd data
 *  @param size			A pointer to the return size of hostcmd buffer
 *  @return      		UAP_SUCCESS
 */
static int
mlan_get_hostcmd_data(FILE * fp, int *ln, t_u8 * buf, t_u16 * size)
{
    t_s32 errors = 0, i;
    t_s8 line[256], *pos, *pos1, *pos2, *pos3;
    t_u16 len;

    while ((pos = mlan_config_get_line(fp, line, sizeof(line), ln))) {
        (*ln)++;
        if (strcmp(pos, "}") == 0) {
            break;
        }

        pos1 = strchr(pos, ':');
        if (pos1 == NULL) {
            printf("Line %d: Invalid hostcmd line '%s'\n", *ln, pos);
            errors++;
            continue;
        }
        *pos1++ = '\0';

        pos2 = strchr(pos1, '=');
        if (pos2 == NULL) {
            printf("Line %d: Invalid hostcmd line '%s'\n", *ln, pos);
            errors++;
            continue;
        }
        *pos2++ = '\0';

        len = a2hex_or_atoi(pos1);
        if (len < 1 || len > MRVDRV_SIZE_OF_CMD_BUFFER) {
            printf("Line %d: Invalid hostcmd line '%s'\n", *ln, pos);
            errors++;
            continue;
        }

        *size += len;

        if (*pos2 == '"') {
            pos2++;
            if ((pos3 = strchr(pos2, '"')) == NULL) {
                printf("Line %d: invalid quotation '%s'\n", *ln, pos);
                errors++;
                continue;
            }
            *pos3 = '\0';
            memset(buf, 0, len);
            memmove(buf, pos2, MIN(strlen(pos2), len));
            buf += len;
        } else if (*pos2 == '\'') {
            pos2++;
            if ((pos3 = strchr(pos2, '\'')) == NULL) {
                printf("Line %d: invalid quotation '%s'\n", *ln, pos);
                errors++;
                continue;
            }
            *pos3 = ',';
            for (i = 0; i < len; i++) {
                if ((pos3 = strchr(pos2, ',')) != NULL) {
                    *pos3 = '\0';
                    *buf++ = (t_u8) a2hex_or_atoi(pos2);
                    pos2 = pos3 + 1;
                } else
                    *buf++ = 0;
            }
        } else if (*pos2 == '{') {
            t_u16 tlvlen = 0, tmp_tlvlen;
            mlan_get_hostcmd_data(fp, ln, buf + len, &tlvlen);
            tmp_tlvlen = tlvlen;
            while (len--) {
                *buf++ = (t_u8) (tmp_tlvlen & 0xff);
                tmp_tlvlen >>= 8;
            }
            *size += tlvlen;
            buf += tlvlen;
        } else {
            t_u32 value = a2hex_or_atoi(pos2);
            while (len--) {
                *buf++ = (t_u8) (value & 0xff);
                value >>= 8;
            }
        }
    }
    return UAP_SUCCESS;
}

/********************************************************
		Global Functions
********************************************************/

/** 
 *  @brief Prepare host-command buffer 
 *  @param fname	path to the config file
 *  @param cmd_name	Command name
 *  @param buf		A pointer to comand buffer    
 *  @return      	UAP_SUCCESS--success, otherwise--fail
 */
int
prepare_host_cmd_buffer(char *fname, char *cmd_name, t_u8 * buf)
{
    t_s8 line[256], cmdname[256], *pos, cmdcode[10];
    apcmdbuf *hostcmd;
    int ln = 0;
    int cmdname_found = 0, cmdcode_found = 0;
    FILE *config_fp;
    int ret = UAP_SUCCESS;

    config_fp = fopen(fname, "r");

    if (!config_fp) {
        printf("Unable to find %s. Exiting...\n", fname);
        return UAP_FAILURE;
    }

    memset(buf, 0, MRVDRV_SIZE_OF_CMD_BUFFER);
    hostcmd = (apcmdbuf *) buf;
    hostcmd->cmd_code = 0xffff;

    sprintf(cmdname, "%s={", cmd_name);
    cmdname_found = 0;
    while ((pos = mlan_config_get_line(config_fp, line, sizeof(line), &ln))) {
        if (strcmp(pos, cmdname) == 0) {
            cmdname_found = 1;
            sprintf(cmdcode, "CmdCode=");
            cmdcode_found = 0;
            while ((pos =
                    mlan_config_get_line(config_fp, line, sizeof(line), &ln))) {
                if (strncmp(pos, cmdcode, strlen(cmdcode)) == 0) {
                    cmdcode_found = 1;
                    hostcmd->cmd_code = a2hex_or_atoi(pos + strlen(cmdcode));
                    hostcmd->size = sizeof(apcmdbuf) - BUF_HEADER_SIZE;
                    mlan_get_hostcmd_data(config_fp, &ln,
                                          buf + sizeof(apcmdbuf),
                                          &hostcmd->size);
                    break;
                }
            }
            if (!cmdcode_found) {
                fprintf(stderr, "uaputl: CmdCode not found in conf file\n");
                ret = UAP_FAILURE;
                goto done;
            }
            break;
        }
    }

    if (!cmdname_found) {
        fprintf(stderr, "uaputl: cmdname '%s' is not found in conf file\n",
                cmd_name);
        ret = UAP_FAILURE;
        goto done;
    }
  done:
    fclose(config_fp);
    return ret;
}
