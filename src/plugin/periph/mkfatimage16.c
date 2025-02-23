/*
 * mkfatimage.c: Make a FAT hdimage pre-loaded with files.
 *
 * Copyright (C) 1995 by Pasi Eronen.
 *
 * Support for variable disk size and 16-bit FAT by Peter Wainwright, 1996.
 *
 * The code in this module is free software; you can redistribute it
 * and/or modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * Note:
 * - This program doesn't support name mangling and does very little
 *   checking for non-DOS filenames.
 * - Disk full condition isn't detected (and probably causes
 *   erratic behaviour).
 * - Duplicate files aren't detected.
 * - There's no real boot sector code, just "dosemu exit" call.
 *
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "disks.h"
//#include "doshelpers.h"

#define MIN(a,b) (((a)<(b))?(a):(b))

extern char _binary_bootnorm_o_bin_end[];
extern char _binary_bootnorm_o_bin_size[];
extern char _binary_bootnorm_o_bin_start[];
extern char _binary_bootsect_o_bin_end[];
extern char _binary_bootsect_o_bin_size[];
extern char _binary_bootsect_o_bin_start[];

/* These can be changed -- at least in theory. In practise, it doesn't
 * seem to work very well (I don't know why).
 */
#define SECTORS_PER_TRACK 17
#define HEADS 4
long sectors_per_track = SECTORS_PER_TRACK;
long heads = HEADS;
long tracks = 36;
long clusters;
long sectors_per_fat;
long p_starting_head = 1;
long p_starting_track = 0;
long p_starting_sector = 1;
long p_starting_absolute_sector = SECTORS_PER_TRACK;
long p_ending_head = HEADS-1;
long p_ending_track;
long p_ending_sector = SECTORS_PER_TRACK;

/* Track 0, head 0 is reserved. */
/* Minus 1 for head 0, track 0 (partition table plus empty space). */
long p_sectors;
static long p_type;
long sectors_per_cluster;
long bytes_per_cluster;

#define ROOT_DIRECTORY_ENTRIES 512

/* I wouldn't touch these. First, the partition information. */

/* Partition status is bootable. */
#define P_STATUS 0x80
/* File system: 12-bit FAT */
#define P_TYPE_12BIT 0x01
/* File system: 16-bit FAT */
#define P_TYPE_16BIT 0x04
/* File system: 16-bit FAT and sectors > 65535 */
#define P_TYPE_32MB  0x06
#define BYTES_PER_SECTOR 512
#define MEDIA_DESCRIPTOR 0xf8
#define FAT_COPIES 2
#define RESERVED_SECTORS 1
#define HIDDEN_SECTORS sectors_per_track
#define SECTORS_PER_ROOT_DIRECTORY ((ROOT_DIRECTORY_ENTRIES*32 + BYTES_PER_SECTOR - 1)/BYTES_PER_SECTOR)


struct input_file
{
  char *filename;
  char dos_filename[12];
  long size;
  time_t mtime;
  int starting_cluster, ending_cluster;
  int size_in_clusters;
};

static FILE *outfile;
static char *bootsect_file=0;
static struct input_file input_files[ROOT_DIRECTORY_ENTRIES];
static int input_file_count = 0;
static unsigned char buffer[BYTES_PER_SECTOR];
static unsigned char *fat;
static unsigned char root_directory[SECTORS_PER_ROOT_DIRECTORY*BYTES_PER_SECTOR];
/* Where to place next input file. */
static int first_available_cluster = 2;

/* Some macros for little-endian data access. */
#define put_word(buffer, data) \
 (((buffer)[0] = ((data) >> 0) & 0xff),\
  ((buffer)[1] = ((data) >> 8) & 0xff))
#define put_dword(buffer, data) \
 (((buffer)[0] = ((data) >> 0) & 0xff),\
  ((buffer)[1] = ((data) >> 8) & 0xff),\
  ((buffer)[2] = ((data) >> 16) & 0xff),\
  ((buffer)[3] = ((data) >> 24) & 0xff))


/* Zero the sector buffer. */
static void clear_buffer(void)
{
  memset(buffer, 0, BYTES_PER_SECTOR);
}

/* Write the sector buffer to disc. */
static void write_buffer(void)
{
  fwrite(buffer, 1, BYTES_PER_SECTOR, outfile);
}

/* Set a FAT entry 'n' to value 'data'. */
static void put_fat(int n, int value)
{
  if (p_type == P_TYPE_12BIT) {
    if (n & 1)
      {
	fat[(n/2)*3+1] = (fat[(n/2)*3+1] & ~0xf0) | ((value & 0x0f) << 4);
	fat[(n/2)*3+2] = (value >> 4) & 0xff;
      }
    else
      {
	fat[(n/2)*3] = value & 0xff;
	fat[(n/2)*3+1] = (fat[n/2*3+1] & ~0x0f) | ((value >> 8) & 0x0f);
      }
  } else if ((p_type == P_TYPE_16BIT) || (p_type == P_TYPE_32MB)) {
    fat[2*n] = value & 0xff;
    fat[2*n+1] = value >> 8;
  } else {
    fprintf(stderr, "Error: FAT type %ld unknown\n", p_type);
    exit(1);
  }
}

/* Set root directory entry 'n'. */
static void put_root_directory(int n, struct input_file *f)
{
  unsigned char *p = &root_directory[n*32];
  struct tm *tm;
  tm = localtime(&f->mtime);
  put_word(&p[24], (((tm->tm_year - 80) & 0b01111111) << 9) |
    (((tm->tm_mon + 1) & 0xf) << 5) |
    (tm->tm_mday & 0x1f));
  put_word(&p[22], ((tm->tm_hour & 0x1f) << 11) |
    ((tm->tm_min & 0x3f) << 5) |
    ((tm->tm_sec>>1) & 0x1f));
  memmove(p, f->dos_filename, 11);
  put_word(&p[26], f->starting_cluster);
  put_dword(&p[28], f->size);
}


/* Get input file information, create DOS filename, and add it to
 * input_files array.
 */
static void add_input_file(char *filename)
{
  char tmp[13], *base, *ext, *p;
  struct stat s;
  struct input_file *i = &input_files[input_file_count];
  int n;

  /* Check that the root directory isn't full (also count volume label). */
  if (input_file_count >= (ROOT_DIRECTORY_ENTRIES-1))
  {
    fprintf(stderr, "mkfatimage: Root directory full!\n");
    return;
  }

  /* Create DOS file name. */
  if ((base = strrchr(filename, '/')) == NULL)
    base = filename;
  else
    base++;
  ext=NULL;
  if (strlen(base) <= 12)
  {
    strcpy(tmp, base);
    base = tmp;
    if ((ext = strchr(tmp, '.')) != NULL)
      *ext++ = '\0';
    else
      ext = base + strlen(base);
  }
  if ((strlen(base) == 0) || (strlen(base) > 8) ||
    (strlen(ext) > 3) || (strchr(ext, '.') != NULL))
  {
    fprintf(stderr, "%s: File name is not DOS-compatible\n", filename);
    return;
  }
  n = snprintf(i->dos_filename, 12, "%-8s%-3s", base, ext);
  assert(n < 12);
  for (p = i->dos_filename; (*p != '\0'); p++)
    *p = toupper(*p);
  i->filename = strdup(filename);

  /* Get the rest of file information. */
  if (stat(filename, &s) != 0)
  {
    fprintf(stderr, "%s: %s\n", filename, strerror(errno));
    return;
  }
  i->size = s.st_size;
  i->mtime = s.st_mtime;
  if (i->size == 0)
  {
    i->size_in_clusters = 0;
    i->starting_cluster = i->ending_cluster = 0;
  }
  else
  {
    i->size_in_clusters = (s.st_size+bytes_per_cluster-1)/bytes_per_cluster;
    i->starting_cluster = first_available_cluster;
    i->ending_cluster = i->starting_cluster + i->size_in_clusters - 1;
    first_available_cluster += i->size_in_clusters;
  }
  input_file_count++;
}


static void usage(void)
{
  fprintf(stderr,
    "Usage:\n"
    "  mkfatimage [-b bsectfile] [{[-t tracks] [-h heads] | -k Kbytes}]\n"
    "             [-l volume-label] [-f outfile] [-p ] [ -r ] [file...]\n");
}


int main(int argc, char *argv[])
{
  int n, m;
  struct image_header *header;
  struct on_disk_partition *part;
  struct on_disk_bpb *bpb;
  int kbytes = -1;
  int raw = 0;
  long total_file_size = 0;
  char *volume_label = NULL;

  outfile = stdout;

  /* Parse command line. */
  if ((argc <= 1) && isatty(STDOUT_FILENO))
  {
    usage();
  }
  while ((n = getopt(argc, argv, "b:l:t:h:k:f:pr")) != EOF)
  {
    switch (n)
    {
    case 'b':
      bootsect_file = strdup(optarg);
      break;
    case 'l':
      volume_label = strdup(optarg);
      for (n = 0; (volume_label[n] != '\0'); n++)
        volume_label[n] = toupper(volume_label[n]);
      if (n > 11)
        volume_label[11] = '\0';
      break;
    case 't':
      if (kbytes > 0)
        usage();
      kbytes = 0;
      tracks = atoi(optarg);
      if (tracks <= 0) {
        fprintf(stderr, "Error: %ld tracks specified - must be positive\n", tracks);
        return 1;
      }
      if (tracks > 1024) {
        fprintf(stderr, "Error: %ld tracks specified - must <= 1024\n", tracks);
        return 1;
      }
      break;
    case 'h':
      if (kbytes > 0)
        usage();
      kbytes = 0;
      heads = atoi(optarg);
      if (heads <= 0) {
        fprintf(stderr, "Error: %ld heads specified - must be positive\n", heads);
        return 1;
      }
      if (heads > 15) {
        fprintf(stderr, "Error: %ld heads specified - must <= 15\n", heads);
        return 1;
      }
      p_ending_head = heads - 1;
      break;
    case 'k':
      if (kbytes != -1)
        usage();
      kbytes = strtol(optarg, 0,0) *2;  /* needed total number of sectors */
      if (kbytes < (SECTORS_PER_TRACK * HEADS *2)) {
        fprintf(stderr, "Error: %d Kbyte specified, must be a reasonable size\n", kbytes);
        return 1;
      }
      tracks = (kbytes + (sectors_per_track * heads) -1) / (sectors_per_track * heads);
      if (tracks > 1024) {
        heads *= 2;
        tracks /= 2;
        if (tracks > 1024) {
          sectors_per_track = 63;
          heads = 15;
          tracks = (kbytes + (sectors_per_track * heads) -1) / (sectors_per_track * heads);
          if (tracks > 1024) {
            fprintf(stderr, "Error: %d Kbyte specified, to big\n", kbytes);
            return 1;
          }
        }
      }
      p_ending_head = heads-1;
      p_starting_absolute_sector = sectors_per_track;
      p_ending_sector = sectors_per_track;
      break;
    case 'f':
      if ((outfile=fopen(optarg, "w")) == 0) {
        fprintf(stderr, "Error: cannot open file %s: %s\n", optarg, strerror(errno));
        return 1;
      }
      break;
    case 'p':
      total_file_size = 1;  /* padding to exact file size */
      break;
    case 'r':
      raw = 1;
      break;
    default:
      usage();
      return 1;
    }
  }
  if (total_file_size) total_file_size = heads*tracks*sectors_per_track*512;
  p_sectors = ((heads*tracks-1)*sectors_per_track);
/*  p_type = ((p_sectors <= 8*0xff7) ? P_TYPE_12BIT : P_TYPE_16BIT); */
  if (p_sectors <= 8 * 0xff7) {
    sectors_per_cluster = 8;
    p_type = P_TYPE_12BIT;
  } else if (p_sectors <= 65535) {
    sectors_per_cluster = 4;
    p_type = P_TYPE_16BIT;
  } else {
    /* sectors_per_cluster must be a power of 2 */
    p_type = P_TYPE_32MB;
    sectors_per_cluster = 1;
    while ((p_sectors / (1 << sectors_per_cluster)) > 65535)
      sectors_per_cluster++;
    sectors_per_cluster = 1 << sectors_per_cluster;
  }
/*  sectors_per_cluster = (p_type == P_TYPE_12BIT) ? 8 : 4; */
  clusters = p_sectors/sectors_per_cluster;
  sectors_per_fat = ((p_type==P_TYPE_12BIT) ?
		     ((3*clusters+1023)/1024) :
		     ((clusters+255)/256));
  p_ending_track = tracks-1;
  bytes_per_cluster = sectors_per_cluster*BYTES_PER_SECTOR;
  if (!(fat = malloc(sectors_per_fat*BYTES_PER_SECTOR))) {
    fprintf(stderr, "Memory error: Cannot allocate fat of %ld sectors\n",
	    sectors_per_fat);
    return 1;
  }

  while (optind < argc)
    add_input_file(argv[optind++]);

  if (!raw) {
    /* Write dosemu image header. */
    clear_buffer();
    header = (void *) buffer;
    strncpy(header->sig, IMAGE_MAGIC, sizeof(header->sig));
    header->heads = heads;
    header->sectors = sectors_per_track;
    header->cylinders = tracks;
    header->header_end = HEADER_SIZE;
    header->dexeflags = 0;
    fwrite(buffer, 1, HEADER_SIZE, outfile);
  }
  /* Write our master boot record */
  clear_buffer();
  memcpy(buffer, _binary_bootnorm_o_bin_start,
    MIN(sizeof(buffer), _binary_bootnorm_o_bin_end-_binary_bootnorm_o_bin_start));
#if 0
  buffer[0] = 0xeb;                     /* Jump to dosemu exit code. */
  buffer[1] = 0x3c;                     /* (jmp 62; nop) */
  buffer[2] = 0x90;
  buffer[62] = 0xb8;                    /* Exec MBR. (mov ax, ...) */
  buffer[63] = DOS_HELPER_MBR;          /* al=0xfe */
  buffer[64] = 0xff;                    /* ah=0xff */
  buffer[65] = 0xcd;                    /* int ... */
  buffer[66] = DOS_HELPER_INT;          /* e6 */
#endif
  part = (void *) &buffer[446];
  part->bootflag = bootsect_file ? P_STATUS : 0;
  part->start_head = p_starting_head;
  part->start_sector = p_starting_sector;
  PTBL_HL_SET(part, start_track, p_starting_track);
  part->OS_type = p_type;
  part->end_head = p_ending_head;
  part->end_sector = p_ending_sector;
  PTBL_HL_SET(part, end_track, p_ending_track);
  part->num_sect_preceding = p_starting_absolute_sector;
  part->num_sectors = p_sectors;
  put_word(&buffer[510], 0xaa55);
  write_buffer();

  /* Write empty sectors to fill track 0. */
  clear_buffer();
  for (n = 2; (n <= p_starting_absolute_sector); n++)
    write_buffer();

  /* Write partition boot sector. */
  if (bootsect_file) {
    FILE *f = fopen(bootsect_file, "rb");
    if (f == NULL) {
      fprintf(stderr, "%s: %s\n", bootsect_file, strerror(errno));
      fprintf(stderr, "taking builtin boot sector\n");
      bootsect_file = 0;
    }
    else {
      clear_buffer();
      fread(buffer, 1, BYTES_PER_SECTOR, f);
      fclose(f);
    }
  }
  if (!bootsect_file) {
    clear_buffer();
    memcpy(buffer, _binary_bootsect_o_bin_start,
        MIN(sizeof(buffer), _binary_bootsect_o_bin_end-_binary_bootsect_o_bin_start));
  }

  bpb = (struct on_disk_bpb *) &buffer[0x0b];
  bpb->bytes_per_sector = BYTES_PER_SECTOR;
  bpb->sectors_per_cluster = sectors_per_cluster;
  bpb->reserved_sectors =  RESERVED_SECTORS;
  bpb->num_fats = FAT_COPIES;
  bpb->num_root_entries = ROOT_DIRECTORY_ENTRIES;
  bpb->num_sectors_small = (p_sectors < 65536L) ? p_sectors : 0;
  bpb->media_type = MEDIA_DESCRIPTOR;
  bpb->sectors_per_fat = sectors_per_fat;
  bpb->sectors_per_track = sectors_per_track;
  bpb->num_heads = heads;

  if (!bootsect_file) {       // Assume writing for Dosemu / FreeDOS so v4 BPB
    memcpy(buffer + 0x03, "IBM  3.3", 8);
    bpb->v340_400_signature = BPB_SIG_V400;
  }

  switch (bpb->v340_400_signature) {
    case BPB_SIG_V400:
      memset(bpb->v400_vol_label, ' ', 11);
      if (volume_label)
        memcpy(bpb->v400_vol_label, volume_label, MIN(strlen(volume_label), 11));
      memcpy(bpb->v400_fat_type,
             p_type == P_TYPE_12BIT ? "FAT12   " : "FAT16   ", 8);
      /* fall through */
    case BPB_SIG_V340:
      bpb->v340_400_drive_number = 0x80;
      bpb->v340_400_flags = 0;
      bpb->v340_400_serial_number = 0x12345678;

      bpb->v331_400_hidden_sectors = HIDDEN_SECTORS;
      bpb->v331_400_num_sectors_large = (p_sectors < 65536L) ? 0 : p_sectors;
      break;

    default: // BPB any of v3.00, v3.20 or v3.31
      /* drive number - some favours of DOS (DR-DOS 3.4x) don't pass it in
       * the boot block and may have other data in the usual 0x1fd position,
       * so try to validate the existing value before updating it with drive
       * number. */
      if (buffer[0x1fd] >= 0x80 && buffer[0x1fd] <= 0x83) {
        buffer[0x1fd] = 0x80;
      }

      if (bpb->num_sectors_small > 0) { // try to be compatible with v3.00
        bpb->v300_320_hidden_sectors = HIDDEN_SECTORS;
      } else {                  // must be v3.20 or v3.31 if we have FAT16B
        bpb->v331_400_hidden_sectors = HIDDEN_SECTORS;
        bpb->v331_400_num_sectors_large = p_sectors;
      }
      break;
  }

  put_word(&buffer[510], 0xaa55);
  write_buffer();

  /* Write FATs. */
  memset(fat, 0, sectors_per_fat*BYTES_PER_SECTOR);
  put_fat(0, 0xfff8);
  put_fat(1, 0xffff);
  for (n = 0; (n < input_file_count); n++)
  {
    for (m = input_files[n].starting_cluster; (m < input_files[n].ending_cluster); m++)
      put_fat(m, m+1);
    put_fat(m, 0xffff);
  }
  for (n = 1; (n <= FAT_COPIES); n++)
    fwrite(fat, 1, sectors_per_fat*BYTES_PER_SECTOR, outfile);
  free(fat);

  /* Write root directory. */
  memset(root_directory, 0, sizeof(root_directory));
  m = 0;
  for (n = 0; (n < input_file_count); n++)
  {
    put_root_directory(m, &input_files[n]);
    m++;
  }
  /* If there's a volume label, add it last to allow booting that needs the
   * first entry. */
  if (volume_label && (n = strlen(volume_label))) {
    unsigned char *p = &root_directory[m*32];
    memcpy(p, volume_label, n);
    memset(p+n, ' ', 11-n);
    p[11] = 0x08;
    m++;
  }
  fwrite(root_directory, 1, SECTORS_PER_ROOT_DIRECTORY*BYTES_PER_SECTOR, outfile);

  /* Write data area. */
  for (n = 0; (n < input_file_count); n++)
  {
    FILE *f = fopen(input_files[n].filename, "rb");
    if (f == NULL)
    {
      fprintf(stderr, "%s: %s\n", input_files[n].filename, strerror(errno));
      continue;
    }
    for (m = 0; (m < (input_files[n].size_in_clusters*sectors_per_cluster)); m++)
    {
      clear_buffer();
      fread(buffer, 1, BYTES_PER_SECTOR, f);
      write_buffer();
    }
    fclose(f);
  }

  /* Size the image and close */
  if (outfile != stdout) {
    if (total_file_size) {
      /* we need padding,
       * but doing it this way it will make holes on an ext2-fs,
       * hence the _actual_ disk usage will not be greater.
       */
      if (!raw)
        total_file_size += HEADER_SIZE;

      fseek(outfile, total_file_size - 1, SEEK_SET);
      fwrite("", 1, 1, outfile);
    }
    fclose(outfile);
  }

  return 0;
}
