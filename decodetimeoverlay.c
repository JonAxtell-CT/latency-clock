/* This program decodes a number of 64-bit clocks that have been
 * encoded into a video feed to allow the latency of the video
 * to be deduced.
 * There can be up to 6 different clocks encoded in the stream.
 * They are:
 *   buffer_time
 *   stream_time
 *   running_time
 *   clock_time
 *   render_time
 *   render_realtime
 * The bits for each 64-bit clock are encoded as 8x8 blocks in the
 * video feed. Decoding just needs to see a single pixel in each block.
 * The 8x8 block is mainly so that the clocks can be seen visually.
 *
 *
 *
 * Copyright (C) 2023 Codethink
 *
 * This utility is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* The clocks that have been encoded in the video frame
 */
typedef struct
{
  __uint32_t buffer_time;
  __uint32_t stream_time;
  __uint32_t running_time;
  __uint32_t clock_time;
  __uint32_t render_time;
  __uint32_t render_realtime;
  __uint32_t latency;
} encoded_clocks_t;

int width;
int height;
int depth;
encoded_clocks_t clocks;

/* Usage and help
 */
static void help_usage()
{
  printf("Usage: timeoverlay-parse <.ppm file>\n");
  exit(0);
}

/* Read a line terminated by a LF
 * The data read is placed in a block of allocated memory and the pointer
 * to it returned in line. The allocated memory will need to be freed by
 * the caller.
 */
static int read_line(FILE* fd, char **line)
{
  size_t len = 0;
  int rd = getline(line, &len, fd);
  if (rd <= 0)
  {
    return 0;
  }
  if (*line == NULL)
  {
    return 0;
  }
  return 1;
}

/* Parse the .PPM file header
 * The header consists of various lines terminated by a LF.
 * The data from the header is returned in width, height and
 * depth is the number of colours.
 */
void parse_header(FILE* fd, int* width, int* height, int* depth)
{
  char *line = NULL;
  int state = 0;

  while (state != 3)
  {
    // Deallocate previous line
    if (line != NULL)
    {
      free(line);
      line = NULL;
    }
    if (read_line(fd, &line))
    {
      switch (state)
      {
        case 0:
        {
          if (strncmp(line, "P6", 2) == 0)
          {
            // printf("P6 ID found\n");
            state = 1;
          }
          break;
        }
        case 1:
        {
          if (*line == '#')
          {
            // printf("Comment: %s", line);    // No \n since already at the end of the comment
            continue;
          }
          sscanf(line, "%d %d", width, height);
          // printf("Size found of %d x %d\n", width, height);
          state = 2;
          break;
        }
        case 2:
        {
          if (*line == '#')
          {
            // printf("Comment: %s", line);    // No \n since already at the end of the comment
            continue;
          }
          // printf("Colour depth found of %s\n", line);
          *depth = atoi(line);
          if (*depth > 255)
          {
            printf("Only max colour depth of 255 handled\n");
            exit(1);
          }
          state = 3;
          break;
        }
      }
    }
  }
  // Deallocate line
  if (line != NULL)
  {
    free(line);
    line = NULL;
  }
}

/* Load the image data from the .PPM file
 * Pixel data is encoded as three bytes per pixel in RGB format.
 * The overall image size should be passed in width and height.
 */
__uint8_t *load_image(FILE* fd, int width, int height)
{
  __uint8_t* image = malloc((width * 3) * height);
  if (image == NULL)
  {
    printf("Not enough memory for the image\n");
    exit(1);
  }
  __uint8_t *image_ptr = image;

  // Reading binary data in format three byte RGB format if color depth is <256
  for (int r = 0; r < height; ++r)
  {
    printf("R=%d-", r);
    for (int c = 0; c < width; ++c)
    {
      __uint8_t r_color = fgetc(fd);
      if (feof(fd))
      {
        printf("Unexpected end at %d %d\n", r, c);
        exit(1);
      }
      __uint8_t g_color = fgetc(fd);
      if (feof(fd))
      {
        printf("Unexpected end at %d %d\n", r, c);
        exit(1);
      }
      __uint8_t b_color = fgetc(fd);
      if (feof(fd))
      {
        printf("Unexpected end at %d %d\n", r, c);
        exit(1);
      }

      *image_ptr++ = r_color;
      *image_ptr++ = g_color;
      *image_ptr++ = b_color;

      printf("%02x%02x%02x", r_color, g_color, b_color);

      if (feof(fd))
      {
        printf("Unexpected end at %d %d\n", r, c);
        exit(1);
      }
    }
    printf("\n");
  }
  return image;
}

/* Read a timestamp from the screen grab.
 * There can be a number of digitally encoded clocks, so the offset to each one
 * is specified in the lineoffset. The image buffer is specified in buf.
 * stride is the width of the image in pixels and pxsize is the number of bytes
 * per pixel, where a 24bit RGB value will be have a pxsize of 3.
 * The decoded value is returned.
 */
static __uint32_t read_timestamp(int lineoffset, __uint8_t *buf, size_t stride, int pxsize)
{
  int bit;
  __uint32_t timestamp = 0;

  buf += (lineoffset * 8 + 4) * stride;

  printf("Color=");
  for (bit = 0; bit < 64; bit++)
  {
    __uint8_t color = buf[bit * pxsize * 8 + 4];
    printf("%.02x ", (unsigned int)color);
    timestamp |= (color & 0x80) ?  (__uint64_t) 1 << (63 - bit) : 0;
  }
  printf("\n");

  return timestamp;
}

/* Decode the timestamp from the image
 * Overall image size is specified in the width and height and the pixel data
 * is specified in image. The decoded clocks is returned in a structure since
 * there are up to 6 different clocks.
 * The encoded data is held as 64bit binary. Each bit is an 8x8 block. There
 * can be up to 6 different clocks encoded in the iamge. The whole lot is placed
 * in the center of the video feed to ease in finding the data.
 */
static void decode_timestamps(int width, int height, __uint8_t* image, encoded_clocks_t* clocks)
{
  int pixel_stride = 3;
  int line_stride = width * pixel_stride;

  __uint8_t *imgdata = image;

  /* Centre Vertically: */
  int vert_offset = (height - (6 * 8)) * line_stride / 2;
  printf("Vertical center=%d\n", vert_offset);
  if (vert_offset < 0)
  {
    vert_offset = 0;
  }
  imgdata += vert_offset;

  /* Centre Horizontally: */
  int horiz_offset = (width - (64 * 8)) * 3 / 2;
  printf("Horizontal center=%d\n", horiz_offset);
  if (horiz_offset < 0)
  {
    horiz_offset = 0;
  }
  imgdata += horiz_offset;

  clocks->buffer_time = read_timestamp(0, image, width * 3, 3);
  clocks->stream_time = read_timestamp(1, image, width * 3, 3);
  clocks->running_time = read_timestamp(2, image, width * 3, 3);
  clocks->clock_time = read_timestamp(3, image, width * 3, 3);
  clocks->render_time = read_timestamp(4, image, width * 3, 3);
  clocks->render_realtime = read_timestamp(5, image, width * 3, 3);

  printf("Read timestamps:\n" \
         "buffer_time = %u\n" \
         "stream_time = %u\n" \
         "running_time = %u\n" \
         "clock_time = %u\n" \
         "render_time = %u\n" \
         "render_realtime = %u\n",
      clocks->buffer_time,
      clocks->stream_time,
      clocks->running_time,
      clocks->clock_time,
      clocks->render_time,
      clocks->render_realtime);

  clocks->latency = clocks->clock_time - clocks->render_realtime;

  printf("Latency: %d\n", clocks->latency);
}

/* Main
 */
int main(int argc, char** argv)
{
  if (argc == 1)
  {
    help_usage();
  }

  FILE *fd = fopen(argv[1], "r");
  if (fd == NULL)
  {
    printf("Unable to open file\n");
    exit(1);
  }
  parse_header(fd, &width, &height, &depth);
  __uint8_t *image = load_image(fd, width, height);
  decode_timestamps(width, height, image, &clocks);
  free(image);
  return 0;
}