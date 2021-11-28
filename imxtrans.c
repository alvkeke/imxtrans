
#include "imxtrans.h"
#include <stdio.h>
#include <errno.h>
#include <sys/io.h>

void print_help(const char *self_name)
{

    fprintf(stderr, "usage: %s [options] <program_name>\n", self_name);
    fprintf(stderr, "avaliable options:\n");
    fprintf(stderr, "\t-e<hex>   : set the program entry point,          default: 0x87800000\n");
    fprintf(stderr, "\t-o<hex>   : set offset from boot device memory,   default: 0x200\n");
    fprintf(stderr, "\t-i<hex>   : set init load region size,            default: 0x1000\n");
    fprintf(stderr, "\t-c<str>   : set certificates and signatures file, default: NO\n");
    fprintf(stderr, "\t-x<str>   : set the output file,                  default: stdout\n");
    fprintf(stderr, "\t-v        : show result data\n\n");
    fprintf(stderr, "example: %s -e87800000 uboot.bin\n\n", self_name);
}

int hex_parse(const char *src, uint32 *dit)
{

    int result = 0;

    while(*src)
    {
        result *= 0x10;
        if (*src >= 'a' && *src <= 'f')
        {
            result += *src - 'a' + 0xa;
        }
        else if (*src >= 'A' && *src <= 'F')
        {
            result += *src - 'A' + 0xa;
        }
        else if (*src >= '0' && *src <= '9')
        {
            result += *src - '0';
        }
        else
        {
            return -1;
        }

        ++src;
    }
    *dit = result;

    return 0;
}

int set_file_ptr(FILE *stream, size_t offset)
{
    int ret = fseek(stream, offset, SEEK_SET);
    if (ret)
    {
        for (int i = 0; i < offset; i++)
        {
            fputc(0x00, stream);
        }
    }
    return ret;
}

int main(int argc, char **argv)
{

    uint32 offset = 0x200;          // default 1KB
    uint32 init_load_size = 0x1000; // default 4KB

    uint32 app_addr = 0x87800000;   // default uboot load place: 0x87800000
    uint32 csf_addr = 0;            // default none

    int verbose = 0;

    char *csf_file = NULL;
    char *app_file = NULL;
    char *out_file = NULL;

    boot_data_t bd = {0};
    ivt_t ivt = {0};

    if (argc < 2)
    {
        print_help(argv[0]);
        return -EINVAL;
    }

    for (int i = 1; i < argc; i++)
    {
        if (argv[i][0] == '-')
        {
            int ret = 0;
            switch (argv[i][1])
            {
            case 'e':
                ret = hex_parse(&argv[i][2], &app_addr);
                break;
            case 'o':
                ret = hex_parse(&argv[i][2], &offset);
                break;
            case 'i':
                ret = hex_parse(&argv[i][2], &init_load_size);
                break;
            case 'c':
                csf_file = &argv[i][2];
                break;
            case 'x':
                out_file = &argv[i][2];
                break;
            case 'v':
                verbose = 1;
                break;
            default:
                break;
            }
            if (ret) 
            {
                fprintf(stderr, "parse error: %s\n", argv[i]);
                print_help(argv[0]);
                return -EINVAL;
            }
        }
        else
        {
            app_file = argv[i];
        }
    }
    
    if (app_file == NULL)
    {
        print_help(argv[0]);
        return -EINVAL;
    }

    if (verbose)
    {
        fprintf(stderr, "address of app in memory: 0x%x\n", app_addr);
        fprintf(stderr, "offset of IVT: 0x%x\n", offset);
        fprintf(stderr, "initial load region size: 0x%x\n", init_load_size);
        fprintf(stderr, "path to certificates and signature file: %s\n", csf_file);
        fprintf(stderr, "path to application: %s\n", app_file);
        fprintf(stderr, "path to output file: %s\n", out_file ? out_file : "stdout");
    }

    // start of the image, count from zero address on boot device.
    bd.start = app_addr - init_load_size;

    size_t fapp_len = 0;
    FILE *fapp = fopen(app_file, "r");
    if (!fapp)
        goto exit_open_fapp;
    
    fseek(fapp, 0, SEEK_END);
    fapp_len = ftell(fapp);
    fseek(fapp, 0, SEEK_SET);
    
    size_t fcsf_len = 0;
    FILE *fcsf = NULL;
    if (csf_file)
    {
        fcsf = fopen(csf_file, "r");
        if (!fcsf)
            goto exit_open_fcsf;
        
        fseek(fcsf, 0, SEEK_END);
        fcsf_len = ftell(fcsf);
        fseek(fcsf, 0, SEEK_SET);
    }
    
    // application size and signature size will be added later.
    bd.length = sizeof(ivt_t) + sizeof(boot_data_t) + sizeof(dcd_table) + fapp_len + fcsf_len;
    if (verbose) 
        fprintf(stderr, "length of image: 0x%x\n", bd.length);

    ivt.header = 0x412000D1;
    ivt.entry = app_addr;
    ivt.p_self = bd.start + offset;
    ivt.p_boot_data = ivt.p_self + sizeof(ivt_t);
    ivt.p_dcd = ivt.p_boot_data + sizeof(boot_data_t);
    ivt.p_csf = csf_addr;

    FILE *fout = stdout;
    if (out_file)
    {
        fout = fopen(out_file, "w");
        if (!fout)
        {
            goto exit_open_fout;
        }
    }

    int ret = set_file_ptr(fout, offset);
    if (ret && verbose)
        fprintf(stderr, "warnning: cannot seek file for IVT offset, write 0x00");

    if (verbose) 
        fprintf(stderr, "pointer after offset: 0x%lx\n", ftell(fout));

    if (!fwrite(&ivt, sizeof(ivt), 1, fout))
        goto exit_write;
    if (!fwrite(&bd, sizeof(bd), 1, fout))
        goto exit_write;
    if (!fwrite(&dcd_table, sizeof(dcd_table), 1, fout))
        goto exit_write;

    ret = set_file_ptr(fout, init_load_size);
    if (ret && verbose)
        fprintf(stderr, "warnning: cannot seek file for application, write 0x00");

    if (verbose)
        fprintf(stderr, "pointer after writing header: 0x%lx\n", ftell(fout));

    int read_len = 0;
    char read_buf[1024];
    while ((read_len = fread(read_buf, 1, sizeof(read_buf), fapp)))
    {
        if (fwrite(read_buf, 1, read_len, fout) != read_len)
            goto exit_write;
    }

    return 0;


exit_write:
    fclose(fout);
exit_open_fout:
    if (fcsf) fclose(fcsf);
exit_open_fcsf:
    fclose(fapp);
exit_open_fapp:
    perror("file operate error");

    return errno;
}
