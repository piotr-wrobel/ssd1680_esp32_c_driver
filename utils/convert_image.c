#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static unsigned char header_data[] = {
	1,1,1,1,1,0,0,0,1,1,1,0,0,0,1,1,
	1,1,0,0,0,0,0,0,0,0,0,0,0,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,0,0,0,1,1,1,0,0,0,1,1,
	1,1,0,0,0,0,0,0,0,0,0,0,0,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,
	};


//~ unsigned char (* wsk)[250];
unsigned char (* wsk)[122];

			


int main(int argc, char *argv[])
{

	char ch;
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
	int width;
	char word[255];
	//while()
	if(fscanf(wp, "= %d;", &width) == 1)
	{
		printf("Width: %d\n",width);
	} else
		printf("Pattern not exist\n");
//~ wsk = (unsigned char (*)[250])header_data;


//~ uint8_t dst_byte = 0,byte = 0,counter = 1;

//~ for(uint8_t j = 0; j < 122; j++)
//~ {
	//~ printf("//y = %d\r\n",j);
	//~ for(uint8_t i = 0; i < 250; i++)
	//~ {
		//~ byte = (*wsk)[i];
		//~ if(byte == 1)
			//~ dst_byte += 128;
		//~ if(((i + 1) % 8 > 0 || i == 0) && i != 249)
		//~ {
			//~ dst_byte = dst_byte >> 1 ;
		//~ }else
		//~ {
			
			//~ if(i == 249)
			//~ {
				//~ dst_byte = dst_byte >> 8 - (250 % 8);
			//~ }			
			//~ printf("0x%.2x, ",dst_byte);
			//~ dst_byte = 0;
						
			//~ if(counter == 8)
			//~ {
				//~ printf("\r\n");
				//~ counter = 0;
			//~ }
			//~ counter++;
			
			//~ if(i == 249)
			//~ {
				//~ printf("\r\n",j);
			//~ }
		//~ }
	//~ }
	//~ wsk++;
//~ }


wsk = (unsigned char (*)[122])header_data;


uint8_t dst_byte = 0,byte = 0,counter = 1;

//for(uint8_t j = 0; j < 250; j++)
//{
//	printf("//y = %d\r\n",j);
//	for(uint8_t i = 0; i < 122; i++)
//	{
//		byte = (*wsk)[i];
//		if(byte == 1)
//			dst_byte += 128;
//		if(((i + 1) % 8 > 0 || i == 0) && i != 121)
//		{
//			dst_byte = dst_byte >> 1 ;
//		}else
//		{
//
//			if(i == 121)
//			{
//				dst_byte = dst_byte >> 8 - (122 % 8);
//			}
//			printf("0x%.2x, ",dst_byte);
//			dst_byte = 0;
//
//			if(counter == 8)
//			{
//				printf("\r\n");
//				counter = 0;
//			}
//			counter++;
//
//			if(i == 121)
//			{
//				printf("\r\n",j);
//			}
//		}
//	}
//	wsk++;
//}

} 
