
#include "imxtrans.h"
#include <stdio.h>
#include <errno.h>
#include <sys/io.h>


static int verbose = 0;

#define debug(fmt, ...) do{\
    if (verbose) fprintf(stderr, fmt "\n", __VA_ARGS__);\
}while(0)

#define error(fmt, ...) do{\
    fprintf(stderr, fmt "\n", __VA_ARGS__);\
}while(0)


#define ALIGN_SIZE (0x400UL)
#define ALIGN_MASK (ALIGN_SIZE - 1)

#define DEFAULT_OFFSET      (0x400U)
#define DEFAULT_INIT_SIZE   (0x1000U)
#define DEFAULT_APP_ADDR    (0x87800000U)
#define DEFAULT_CSF_ADDR    (0x00000000U);


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


/**
 * @brief Set the file ptr object, this method can be used to move pointer forward only!!
 * 
 * @param stream file pointer
 * @param offset offset from 0, MUST be bigger than ftell(stream)
 * @return int 0 for success, -1 for error.
 */
int set_file_ptr(FILE *stream, size_t offset)
{
    long currp = ftell(stream);
    if (currp > offset) 
    {
        errno = EINVAL;
        return -1;
    }

    int ret = fseek(stream, offset, SEEK_SET);
    if (ret)
    {
        debug("warnning: cannot seek file, write 0x00", NULL);
        for (int i = 0; i < offset-currp; i++)
        {
            if (fputc(0x00, stream) == -1)
                return -1;
        }
    }

    return 0;
}

int fill_file_zero(FILE *stream, size_t n)
{
    for (int i=0; i<n; i++)
    {
        if (fputc(0x00, stream) == -1) return -1;        
    }
    return 0;
}

int main(int argc, char **argv)
{

    uint32 offset = DEFAULT_OFFSET;
    uint32 init_load_size = DEFAULT_INIT_SIZE;

    uint32 app_addr = DEFAULT_APP_ADDR;
    uint32 csf_addr = DEFAULT_CSF_ADDR;


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
                error("parse error: %s", argv[i]);
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

    debug("address of app in memory: 0x%x", app_addr);
    debug("offset of IVT: 0x%x", offset);
    debug("initial load region size: 0x%x", init_load_size);
    debug("path to certificates and signature file: %s", csf_file);
    debug("path to application: %s", app_file);
    debug("path to output file: %s", out_file ? out_file : "stdout");
    
    // start of the image, count from zero address on boot device.
    bd.start = app_addr - init_load_size;
    debug("boot_data.start: 0x%x", bd.start);

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

    size_t align_fapp_len, align_fcsf_len;

    align_fapp_len = fapp_len & ALIGN_MASK ? (fapp_len + ALIGN_SIZE) & (~ALIGN_MASK) : fapp_len;
    align_fcsf_len = fcsf_len & ALIGN_MASK ? (fcsf_len + ALIGN_SIZE) & (~ALIGN_MASK) : fcsf_len;
    debug("origin app len: 0x%lx, aligned len: 0x%lx", fapp_len, align_fapp_len);
    debug("origin csf len: 0x%lx, aligned len: 0x%lx", fcsf_len, align_fcsf_len);

    // application size and signature size will be added later.
    bd.length = init_load_size + align_fapp_len + align_fcsf_len;
    debug("length of image: 0x%x", bd.length);

    ivt.header = 0x412000D1;
    ivt.entry = app_addr;
    debug("ivt.entry: 0x%x", ivt.entry);
    ivt.p_self = bd.start + offset;
    debug("ivt.self: 0x%x", ivt.p_self);
    ivt.p_boot_data = ivt.p_self + sizeof(ivt_t);
    debug("ivt.boot_data: 0x%x", ivt.p_boot_data);
    ivt.p_dcd = ivt.p_boot_data + sizeof(boot_data_t);
    debug("ivt.dcd: 0x%x", ivt.p_dcd);
    ivt.p_csf = csf_addr;
    debug("ivt.csf: 0x%x", ivt.p_csf);

    FILE *fout = stdout;
    if (out_file)
    {
        fout = fopen(out_file, "w");
        if (!fout)
        {
            goto exit_open_fout;
        }
    }

    if (set_file_ptr(fout, offset))
        goto exit_write;

    debug("pointer after offset: 0x%lx", ftell(fout));

    if (!fwrite(&ivt, sizeof(ivt), 1, fout))
        goto exit_write;
    if (!fwrite(&bd, sizeof(bd), 1, fout))
        goto exit_write;
    if (!fwrite(&dcd_table, sizeof(dcd_table), 1, fout))
        goto exit_write;

    if (set_file_ptr(fout, init_load_size))
        goto exit_write;

    debug("pointer after writing header: 0x%lx", ftell(fout));

    int read_len = 0;
    char read_buf[1024];
    while ((read_len = fread(read_buf, 1, sizeof(read_buf), fapp)))
    {
        if (fwrite(read_buf, 1, read_len, fout) != read_len)
            goto exit_write;
    }

    if (fill_file_zero(fout, align_fapp_len - fapp_len))
        goto exit_write;

    if (fcsf)
    {
        while ((read_len = fread(read_buf, 1, sizeof(read_buf), fcsf)))
        {
            if (fwrite(read_buf, 1, read_len, fout) != read_len)
                goto exit_write;
        }
        if (fill_file_zero(fout, align_fcsf_len - fcsf_len))
            goto exit_write;
    }

    fclose(fapp);
    if (fcsf) fclose(fcsf);
    fclose(fout);

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
