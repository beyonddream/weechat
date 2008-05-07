/*
 * Copyright (c) 2003-2008 by FlashCode <flashcode@flashtux.org>
 * See README for License detail, AUTHORS for developers list.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/* xfer-file.c: file functions for xfer plugin */


#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <sys/wait.h>

#include "../weechat-plugin.h"
#include "xfer.h"
#include "xfer-file.h"
#include "xfer-buffer.h"
#include "xfer-config.h"


/*
 * xfer_file_resume: resume a download
 *                   return 1 if ok, 0 if not resumable
 */

int
xfer_file_resume (struct t_xfer *xfer, char *filename)
{
    struct stat st;
    
    if (!weechat_config_boolean (xfer_config_file_auto_resume))
        return 0;
    
    if (access (filename, W_OK) == 0)
    {
        if (stat (filename, &st) != -1)
        {
            if ((unsigned long) st.st_size < xfer->size)
            {
                xfer->start_resume = (unsigned long) st.st_size;
                xfer->pos = xfer->start_resume;
                xfer->last_check_pos = xfer->start_resume;
                return 1;
            }
        }
    }
    
    /* not resumable */
    return 0;
}

/*
 * xfer_file_find_filename: find local filename for a xfer
 *                          if type if file/recv, add a suffix (like .1) if needed
 *                          if download is resumable, set "start_resume" to good value
 */

void
xfer_file_find_filename (struct t_xfer *xfer)
{
    char *weechat_home, *dir1, *dir2, *filename2, *dir_separator;
    
    if (!XFER_IS_FILE(xfer->type))
        return;
    
    dir1 = weechat_string_replace (weechat_config_string (xfer_config_file_download_path),
                                                          "~",
                                                          getenv ("HOME"));
    if (!dir1)
        return;
    
    weechat_home = weechat_info_get ("weechat_dir");
    if (!weechat_home)
    {
        free (dir1);
        return;
    }
    dir2 = weechat_string_replace (dir1, "%h", weechat_home);
    if (!dir2)
    {
        free (dir1);
        return;
    }
    
    xfer->local_filename = malloc (strlen (dir2) +
                                   strlen (xfer->remote_nick) +
                                   strlen (xfer->filename) + 4);
    if (!xfer->local_filename)
        return;
    
    strcpy (xfer->local_filename, dir2);
    dir_separator = weechat_info_get("dir_separator");
    if (dir_separator
        && (xfer->local_filename[strlen (xfer->local_filename) - 1] != dir_separator[0]))
        strcat (xfer->local_filename, dir_separator);
    strcat (xfer->local_filename, xfer->remote_nick);
    strcat (xfer->local_filename, ".");
    strcat (xfer->local_filename, xfer->filename);
    
    if (dir1)
        free (dir1);
    if (dir2 )
        free (dir2);
    
    /* file already exists? */
    if (access (xfer->local_filename, F_OK) == 0)
    {
        if (xfer_file_resume (xfer, xfer->local_filename))
            return;
        
        /* if auto rename is not set, then abort xfer */
        if (!xfer_config_file_auto_rename)
        {
            xfer_close (xfer, XFER_STATUS_FAILED);
            xfer_buffer_refresh (WEECHAT_HOTLIST_MESSAGE);
            return;
        }
        
        filename2 = malloc (strlen (xfer->local_filename) + 16);
        if (!filename2)
        {
            xfer_close (xfer, XFER_STATUS_FAILED);
            xfer_buffer_refresh (WEECHAT_HOTLIST_MESSAGE);
            return;
        }
        xfer->filename_suffix = 0;
        do
        {
            xfer->filename_suffix++;
            sprintf (filename2, "%s.%d",
                     xfer->local_filename,
                     xfer->filename_suffix);
            if (access (filename2, F_OK) == 0)
            {
                if (xfer_file_resume (xfer, filename2))
                    break;
            }
            else
                break;
        }
        while (1);
        
        free (xfer->local_filename);
        xfer->local_filename = strdup (filename2);
        free (filename2);
    }
}

/*
 * xfer_file_calculate_speed: calculate xfer speed (for files only)
 */

void
xfer_file_calculate_speed (struct t_xfer *xfer, int ended)
{
    time_t local_time, elapsed;
    unsigned long bytes_per_sec_total;
    
    local_time = time (NULL);
    if (ended || local_time > xfer->last_check_time)
    {
        if (ended)
        {
            /* calculate bytes per second (global) */
            elapsed = local_time - xfer->start_transfer;
            if (elapsed == 0)
                elapsed = 1;
            xfer->bytes_per_sec = (xfer->pos - xfer->start_resume) / elapsed;
            xfer->eta = 0;
        }
        else
        {
            /* calculate ETA */
            elapsed = local_time - xfer->start_transfer;
            if (elapsed == 0)
                elapsed = 1;
            bytes_per_sec_total = (xfer->pos - xfer->start_resume) / elapsed;
            if (bytes_per_sec_total == 0)
                bytes_per_sec_total = 1;
            xfer->eta = (xfer->size - xfer->pos) / bytes_per_sec_total;
            
            /* calculate bytes per second (since last check time) */
            elapsed = local_time - xfer->last_check_time;
            if (elapsed == 0)
                elapsed = 1;
            xfer->bytes_per_sec = (xfer->pos - xfer->last_check_pos) / elapsed;
        }
        xfer->last_check_time = local_time;
        xfer->last_check_pos = xfer->pos;
    }
}
