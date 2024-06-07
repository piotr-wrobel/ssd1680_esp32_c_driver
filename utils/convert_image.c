#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>


int main(int argc, char *argv[])
{

	uint8_t * buff;
	int width = 0, height = 0, pixels = 0, bit = 0;
	char phraze1[255];
	FILE *wp;

	if(argc != 2)
	{
		printf("Use: %s gimp_indexed_image_as_c_header_file.h\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	if ((wp = fopen(argv[1], "r")) == NULL)
	{
		printf("Cannot open file %s\n", argv[1]);
		exit(EXIT_FAILURE);
	}
	rewind(wp);

	while(fscanf(wp, "%s", phraze1) == 1)
	{
		if(!strcmp(phraze1,"width") && fscanf(wp,"%s",phraze1) && !strcmp(phraze1,"="))
		{
			fscanf(wp,"%d",&width);
		}
		if(!strcmp(phraze1,"height") && fscanf(wp,"%s",phraze1) && !strcmp(phraze1,"="))
		{
			fscanf(wp,"%d",&height);
		}

		if(!strcmp(phraze1,"header_data[]") && fscanf(wp,"%s",phraze1) && !strcmp(phraze1,"=") && fscanf(wp,"%s",phraze1) && !strcmp(phraze1,"{"))
		{

			printf("Found data !\n");
			if(width > 0 && height > 0)
			{
				pixels = width * height;
				buff = malloc(pixels);
				printf("Image size: %d x %d, pixels: %d\n", width, height, pixels);

				for(int i = 0; i < pixels; i++)
				{
					if (fscanf(wp,"%d,", &bit) == 1)
					{
						printf("%d",bit);

					} else
						bit = 0;
					buff[i] = bit;
				}
			} else
			{
				printf("Patterns not exist\n");
				exit(EXIT_FAILURE);
			}
		}
   }


uint8_t (* wsk)[width] = (uint8_t (*)[width])buff;


uint8_t dst_byte = 0,byte = 0,counter = 1;

for(int j = 0; j < height + 1; j++)
{
	printf("//y = %d\r\n",j);
	for(int i = 0; i < width; i++)
	{
		byte = (*wsk)[i];
		if(byte == 1)
			dst_byte += 128;
		if(((i + 1) % 8 > 0 || i == 0) && i != width - 1)
		{
			dst_byte = dst_byte >> 1 ;
		}else
		{

			if(i == width - 1)
			{
				dst_byte = dst_byte >> (8 - (width % 8));
			}
			printf("0x%.2x, ",dst_byte);
			dst_byte = 0;

			if(counter == 8)
			{
				printf("\r\n");
				counter = 0;
			}
			counter++;

			if(i == width - 1)
			{
				printf("\r\n");
			}
		}
	}
	wsk++;
}

free(buff);

} 
