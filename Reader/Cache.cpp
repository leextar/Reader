#include "StdAfx.h"
#include "Cache.h"
#include "Keyset.h"
#include "Upgrade.h"
#include <stdio.h>
#include <string.h>
#include <shlwapi.h>


extern UINT GetCacheVersion(void);

Cache::Cache(TCHAR* file)
{
    GetModuleFileName(NULL, m_file_name, sizeof(TCHAR)*(MAX_PATH-1));
    for (int i=_tcslen(m_file_name)-1; i>=0; i--)
    {
        if (m_file_name[i] == _T('\\'))
        {
            memcpy(&m_file_name[i+1], file, (_tcslen(file)+1)*sizeof(TCHAR));
            break;
        }
    }
    m_buffer = NULL;
    m_size = 0;
}


Cache::~Cache(void)
{
    if (m_buffer)
    {
        free(m_buffer);
        m_buffer = NULL;
    }
    m_size = 0;
}

bool Cache::init()
{
    FILE* fp = NULL;

    if (!PathFileExists(m_file_name)) // not exist
    {
        if (!default_header())
            return false;
    }
    else
    {
        if (read())
        {
            // check data
            if (!check_cache())
                return false;
            return true;
        }
        return false;
    }

    return true;
}

bool Cache::exit()
{
    FILE* fp = NULL;
    bool result = true;

    if (m_buffer)
    {
        result = write();

        // free
        free(m_buffer);
        m_buffer = NULL;
        m_size = 0;
    }

    return result;
}

bool Cache::save()
{
    return write();
}

header_t* Cache::get_header()
{
    return (header_t*)m_buffer;
}

item_t* Cache::get_item(int item_id)
{
    header_t* header = get_header();
    int offset = sizeof(header_t) + item_id*sizeof(item_t);
    if (item_id >= header->item_count)
        return NULL;
    return (item_t*)((char*)m_buffer + offset);
}

item_t* Cache::open_item(int item_id)
{
    header_t* header = get_header();
    item_t* item = get_item(item_id);

    // move to index 0
    move_item(item->id, 0);
    item = get_item(0);

    if (header && item)
    {
        header->item_id = item_id;
    }

    return item;
}

#if ENABLE_MD5
item_t* Cache::new_item(u128_t* item_md5, TCHAR* file_name)
#else
item_t* Cache::new_item(TCHAR* file_name)
#endif
{
    header_t* header = NULL;
#if ENABLE_MD5
    item_t* item = find_item(item_md5, file_name);
#else
    item_t* item = find_item(file_name);
#endif
    int item_id = -1;
    void *oldAddr = NULL;

    // already exist
    if (item)
    {
        //return item;
        return NULL;
    }

    // new item
    oldAddr = m_buffer;
    m_buffer = realloc(m_buffer, m_size+sizeof(item_t));
    if (!m_buffer)
        return NULL;
    if (oldAddr != m_buffer)
        update_addr();
    m_size += sizeof(item_t);
    header = get_header();
    item_id = header->item_count++;

    // init item
    item = get_item(item_id);
    memset(item, 0, sizeof(item_t));
    item->id = item_id;
#if ENABLE_MD5
    memcpy(&item->md5, item_md5, sizeof(u128_t));
#endif
    memcpy(item->file_name, file_name, sizeof(TCHAR) * MAX_PATH);

    // move to index 0
    move_item(item->id, 0);
    item = get_item(0);

    return item;
}

#if ENABLE_MD5
item_t* Cache::find_item(u128_t* item_md5, TCHAR* file_name)
#else
item_t* Cache::find_item(TCHAR* file_name)
#endif
{
    header_t* header = get_header();
    item_t* item = NULL;

    if (header->item_count <= 0)
        return NULL;

    for (int i=0; i<header->item_count; i++)
    {
        item = get_item(i);
#if ENABLE_MD5
        if (0 == memcmp(&item->md5, item_md5, sizeof(u128_t)))
        {
            // update file name
            if (0 != _tcscmp(item->file_name, file_name))
            {
                memcpy(item->file_name, file_name, sizeof(TCHAR) * MAX_PATH);
            }
            return item;
        }
#else
        if (0 == _tcscmp(item->file_name, file_name))
        {
            return item;
        }
#endif
    }
    return NULL;
}

bool Cache::delete_item(int item_id)
{
    header_t* header = get_header();
    item_t* item = get_item(item_id);
    if (!item)
        return false;

    for (int i=item_id+1; i<header->item_count; i++)
    {
        item_t* item_1 = get_item(i);
        item_t* item_2 = get_item(i-1);
        item_1->id--;
        memcpy(item_2, item_1, sizeof(item_t));
    }
    header->item_count--;
    m_size -= sizeof(item_t);
    return true;
}

bool Cache::delete_all_item(void)
{
    header_t* header = get_header();

    header->item_count = 0;
    header->item_id = -1;
    m_size = sizeof(header_t);
    return true;
}

header_t* Cache::default_header()
{
    header_t* header = NULL;

    if (!m_buffer)
    {
        m_size = sizeof(header_t);
        m_buffer = malloc(m_size);
        if (!m_buffer)
            return NULL;
        memset(m_buffer, 0, m_size);
        header = (header_t*)m_buffer;
        header->item_id = -1;
    }
    else
    {
        header = (header_t*)m_buffer;
    }

    // default flag
    header->flag = CACHE_FIXED;
    header->header_size = sizeof(header_t);
    header->item_size = sizeof(item_t);
    
    // default font
    static HFONT hFont = NULL;
    static LOGFONT lf;
    if (!hFont)
    {
        hFont = CreateFont(20, 0, 0, 0,
            FW_THIN,false,false,false,
            ANSI_CHARSET,OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS, PROOF_QUALITY,
            DEFAULT_PITCH | FF_SWISS,_T("Consolas"));
        GetObject(hFont, sizeof(lf), &lf);
    }
    memcpy(&header->font, &lf, sizeof(lf));
    header->font_color = 0x00;      // black

    // default rect
    int width = 300;
    int height = 500;
    header->rect.left = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
    header->rect.top = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;
    header->rect.right = header->rect.left + width;
    header->rect.bottom = header->rect.top + height;

    // default bk color
    header->bg_color = 0x00ffffff;  // White
    header->alpha = 0xff;

    header->line_gap = 5;
    header->internal_border = 0;
    header->version = GetCacheVersion();

    // default others
    header->wheel_speed = 1;
    header->page_mode = 1;
    header->autopage_mode = 0;
    header->uElapse = 3000;

    header->bg_image.enable = 0;
    header->disable_lrhide = 1;

    header->show_systray = 0;
    header->hide_taskbar = 0;

    // set flag for dpi;
    header->isDefault = 1;

    // default hotkey
    KS_GetDefaultKeyBuff(header->keyset);

    // default chapter rule
    header->chapter_rule.rule = 0;

    // default tags
#if ENABLE_TAG
    for (int i=0; i<MAX_TAG_COUNT; i++)
    {
        memcpy(&header->tags[i].font, &lf, sizeof(lf));
        header->tags[i].bg_color = 0x00FFFFFF;
        header->tags[i].enable = 0;
        header->tags[i].font_color = 0x00;
    }
#endif
	
    return header;
}

bool Cache::add_mark(item_t *item, int value)
{
    int i;

    if (!item)
        return false;
    if (item->mark_size >= MAX_MARK_COUNT)
        return false;
    for (i=0; i<item->mark_size; i++)
    {
        if (value == item->mark[i])
            return false;
    }
    item->mark[item->mark_size] = value;
    item->mark_size++;
    return true;
}

bool Cache::del_mark(item_t *item, int index)
{
    if (!item)
        return false;
    if (item->mark_size <= 0)
        return false;
    if (index >= item->mark_size)
        return false;

    // delete
    if (item->mark_size - index - 1 > 0)
    {
        memcpy(item->mark+index, item->mark+index+1, (item->mark_size-index-1)*sizeof(int));
    }
    item->mark_size--;
    item->mark[item->mark_size] = 0;
    return true;
}

bool Cache::move_item(int from, int to)
{
    // from and to is exist
    item_t temp = {0};
    item_t* from_item = get_item(from);
    item_t* to_item = get_item(to);
    if (!from_item || !to_item)
    {
        return false;
    }

    if (from == to)
    {
        return true;
    }
    else if (from > to)
    {
        memcpy(&temp, from_item, sizeof(item_t));
        for (int i=from-1; i>=to; i--)
        {
            item_t* item_1 = get_item(i);
            item_t* item_2 = get_item(i+1);
            item_1->id++;
            memcpy(item_2, item_1, sizeof(item_t));
        }
        temp.id = to;
        memcpy(to_item, &temp, sizeof(item_t));
    }
    else
    {
        memcpy(&temp, from_item, sizeof(item_t));
        for (int i=from+1; i<to; i++)
        {
            item_t* item_1 = get_item(i);
            item_t* item_2 = get_item(i-1);
            item_1->id--;
            memcpy(item_2, item_1, sizeof(item_t));
        }
        temp.id = to;
        memcpy(to_item, &temp, sizeof(item_t));
    }
    return true;
}

bool Cache::read()
{
    HANDLE hFile = NULL;
    BOOL bErrorFlag = FALSE;
    DWORD dwFileSize = 0;
    DWORD dwBytesRead = 0;

    hFile = CreateFile(m_file_name,                // file to open
        GENERIC_READ,          // open for reading
        FILE_SHARE_READ,       // share for reading
        NULL,                  // default security
        OPEN_EXISTING,         // existing file only
        FILE_ATTRIBUTE_HIDDEN, // hidden file
        NULL);                 // no attr. template

    if (hFile == INVALID_HANDLE_VALUE) 
    {
        return false;
    }

    dwFileSize = GetFileSize(hFile, NULL);
    if (INVALID_FILE_SIZE == dwFileSize)
    {
        CloseHandle(hFile);
        return false;
    }

    m_buffer = malloc(dwFileSize);
    if (!m_buffer)
    {
        CloseHandle(hFile);
        return false;
    }
    m_size = dwFileSize;

    bErrorFlag = ReadFile(hFile,
        m_buffer,
        dwFileSize,
        &dwBytesRead,
        NULL);

    if (FALSE == bErrorFlag)
    {
        CloseHandle(hFile);
        return false;
    }
    else
    {
        if (dwBytesRead != m_size)
        {
            CloseHandle(hFile);
            return false;
        }
    }

    CloseHandle(hFile);
    return true;
}

bool Cache::write()
{
    HANDLE hFile = NULL;
    BOOL bErrorFlag = FALSE;
    DWORD dwBytesWritten = 0;

    hFile = CreateFile(m_file_name,                // name of the write
        GENERIC_WRITE,          // open for writing
        0,                      // do not share
        NULL,                   // default security
        CREATE_ALWAYS,          // create new file only
        FILE_ATTRIBUTE_HIDDEN,  // hidden file
        NULL);                  // no attr. template
    if (hFile == INVALID_HANDLE_VALUE) 
    {
        return false;
    }

    bErrorFlag = WriteFile( 
        hFile,           // open file handle
        m_buffer,        // start of data to write
        m_size,          // number of bytes to write
        &dwBytesWritten, // number of bytes that were written
        NULL);           // no overlapped structure

    if (FALSE == bErrorFlag)
    {
        CloseHandle(hFile);
        return false;
    }
    else
    {
        if (dwBytesWritten != m_size)
        {
            CloseHandle(hFile);
            return false;
        }
    }

    CloseHandle(hFile);
    return true;
}

void Cache::update_addr(void)
{
#if ENABLE_NETWORK
    extern Upgrade _Upgrade;
#endif
    header_t* header = NULL;

    header = (header_t*)m_buffer;

    KS_UpdateBuffAddr(header->keyset);
#if ENABLE_NETWORK
    _Upgrade.SetProxy(&header->proxy);
#endif
}

bool Cache::check_cache(void)
{
    header_t *header;

    header = (header_t *)m_buffer;

    if (!header)
        return false;

    if (header->flag == CACHE_REMOVE)
    {
        // remove old cache data
        DeleteFile(m_file_name);
        free(m_buffer);
        m_buffer = NULL;
        if (!default_header())
            return false;
    }
    else if (header->flag == CACHE_FIXED)
    {
        // need fixed
        if (header->header_size != sizeof(header_t)
            || header->item_size != sizeof(item_t))
        {
            void *buffer;
            int i, offset;
            void *item;
            int len;

            // backup old buffer
            buffer = m_buffer;

            // malloc new buffer
            m_size = sizeof(header_t) + (header->item_count * sizeof(item_t));
            m_buffer = malloc(m_size);
            memset(m_buffer, 0, m_size);

            // default new buffer
            if (!default_header())
            {
                free(buffer);
                return false;
            }
            get_header()->item_count = header->item_count;

            // copy header
            len = sizeof(header_t) > header->header_size ? header->header_size : sizeof(header_t);
            memcpy(get_header(), header, len);
            get_header()->header_size = sizeof(header_t);
            get_header()->item_size = sizeof(item_t);
            get_header()->item_count = header->item_count;

            // copy item list
            for (i=0; i<header->item_count; i++)
            {
                offset = header->header_size + i * header->item_size;
                item = (void*)((char*)buffer + offset);
                len = sizeof(item_t) > header->item_size ? header->item_size : sizeof(item_t);

                memcpy(get_item(i), item, len);
            }

            free(buffer);
        }
    }
    else
    {
        // remove old cache data
        DeleteFile(m_file_name);
        free(m_buffer);
        m_buffer = NULL;
        if (!default_header())
            return false;
    }

    return true;
}
