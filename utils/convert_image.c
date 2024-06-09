#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

char * strtoupper(char * str)
{
//	while(*str != 0)
//	{
//		*str = toupper(*str);
//		str++;
//	}

	return str;
}

int main(int argc, char *argv[])
{

	uint8_t * buff;
	int width = 0, height = 0, pixels = 0, bit = 0;
	char phraze1[255];
	FILE * input_file;
	FILE * output_file;

	if(argc < 3 || argc > 4)
	{
		printf("Use: %s gimp_indexed_image_as_c_header_file.h table_name [OPTIONAL_HEADER_TEMPLATE]\r\n", argv[0]);
		printf("OPTIONAL_HEADER_TEMPLATE - if omitted, it takes the value: COMPONENTS_LIB_SSD1680_INCLUDE_BITMAPS_TABLE_NAME_WIDTH_HEIGHT_H_\r\n");
		exit(EXIT_FAILURE);
	}

	if( strlen(argv[1]) > 200 )
	{
		printf("The length of the input file name is greater than 200 characters\r\n");
		exit(EXIT_FAILURE);
	}


	printf("input: %s\r\n", argv[1]);
	char input_file_name[255], input_path[255];
	char * input_path_ptr;
	strncpy(input_file_name, argv[1], strlen(argv[1]) + 1);
	input_path_ptr = strrchr(input_file_name,'/');
	if(input_path_ptr != NULL)
	{
		//printf("%d,%d\r\n",(int)input_path_ptr,(int)input_file_name);
		printf("Path length: %d\r\n", (int)(input_path_ptr - input_file_name + 1));
		strncpy(input_path, input_file_name, (int)(input_path_ptr - input_file_name + 1));
	} else
	{
		input_path[0] = '\0';
	}
	printf("Path: %s\r\n", input_path);


	if ((input_file = fopen(argv[1], "r")) == NULL)
	{
		printf("Cannot open file %s\r\n", argv[1]);
		exit(EXIT_FAILURE);
	}
	rewind(input_file);


	char table_name[11];
	int table_name_length = strlen(argv[2]);
	if(table_name_length > 10) table_name_length = 10;
	printf("table_name_length: %d\r\n", table_name_length);
	strncpy(table_name, argv[2], table_name_length + 1);




//	printf("output: %s\r\n", argv[2]);
//	printf("table: %s\r\n", argv[3]);



	while(fscanf(input_file, "%s", phraze1) == 1)
	{
		if(!strcmp(phraze1,"width") && fscanf(input_file,"%s",phraze1) && !strcmp(phraze1,"="))
		{
			fscanf(input_file,"%d",&width);
		}
		if(!strcmp(phraze1,"height") && fscanf(input_file,"%s",phraze1) && !strcmp(phraze1,"="))
		{
			fscanf(input_file,"%d",&height);
		}

		if(!strcmp(phraze1,"header_data[]") && fscanf(input_file,"%s",phraze1) && !strcmp(phraze1,"=") && fscanf(input_file,"%s",phraze1) && !strcmp(phraze1,"{"))
		{

			printf("Found data !\r\n");
			if(width > 0 && height > 0)
			{
				pixels = width * height;
				buff = malloc(pixels);
				if(buff == NULL)
				{
					printf("Cannot allocate memory!\r\n");
					exit(EXIT_FAILURE);
				}
				printf("Image size: %d x %d, pixels: %d\r\n", width, height, pixels);

				for(int i = 0; i < pixels; i++)
				{
					if (fscanf(input_file,"%d,", &bit) == 1)
					{
						//printf("%d",bit);

					} else
						bit = 0;
					buff[i] = bit;
				}
			} else
			{
				printf("Patterns not exist\r\n");
				exit(EXIT_FAILURE);
			}
		}
	}
	if( fclose(input_file) != 0 )
		printf("Cannot close input file!\r\n");

	printf("\r\n");

	uint8_t (* wsk)[width] = (uint8_t (*)[width])buff;

	char output_file_name[255], strbuff[255];
	strcpy(output_file_name, input_path);
	strcat(output_file_name, "image_");
	strcat(output_file_name, table_name);
	strcat(output_file_name, "_");
	sprintf(strbuff,"%d",width);
	strcat(output_file_name, strbuff);
	strcat(output_file_name, "_");
	sprintf(strbuff,"%d",height);
	strcat(output_file_name, strbuff);
	strcat(output_file_name, ".h");
	uint8_t dst_byte = 0,byte = 0,counter = 1;
	if ((output_file = fopen(output_file_name, "w+")) == NULL)
	{
		printf("Cannot open/create file %s\r\n", output_file_name);
		exit(EXIT_FAILURE);
	}

	if( argc == 4 )
	{
		fprintf(output_file, "#ifndef %s_%s_%d_%d_H_\r\n", strtoupper(argv[3]), strtoupper(table_name), width, height);
		fprintf(output_file, "#define %s_%s_%d_%d_H_\r\n", strtoupper(argv[3]), strtoupper(table_name), width, height);
	} else
	{
		fprintf(output_file, "#ifndef COMPONENTS_LIB_SSD1680_INCLUDE_BITMAPS_%s_%d_%d_H_\r\n", strtoupper(table_name), width, height);
		fprintf(output_file, "#define COMPONENTS_LIB_SSD1680_INCLUDE_BITMAPS_%s_%d_%d_H_\r\n", strtoupper(table_name), width, height);
	}

	fprintf(output_file, "\r\n#include <stdint.h>\r\n\r\n");
	fprintf(output_file, "uint8_t image_%s_%d_%d[] =\r\n{\r\n", table_name, width, height);

	for(int j = 0; j < height + 1; j++)
	{
		fprintf(output_file, "\t//y = %d\r\n\t",j);
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
				fprintf(output_file, "0x%.2x, ",dst_byte);
				dst_byte = 0;

				if(counter == 8)
				{
					fprintf(output_file, "\r\n\t");
					counter = 0;
				}
				counter++;

				if(i == width - 1)
				{
					fprintf(output_file, "\r\n");
				}
			}
		}
		wsk++;
	}
	fprintf(output_file, "}\r\n\r\n");
	if( argc == 4 )
	{
		fprintf(output_file, "#endif /* %s_%s_%d_%d_H_ */\r\n", strtoupper(argv[3]), strtoupper(table_name), width, height);
	} else
	{
		fprintf(output_file, "#endif /* COMPONENTS_LIB_SSD1680_INCLUDE_BITMAPS_%s_%d_%d_H_ */\r\n", strtoupper(table_name), width, height);
	}

	free(buff);

	if( fclose(output_file) != 0 )
		printf("Cannot close output file file!\r\n");

} 
