/* config.h.  Generated from config.h.in by configure.  */
/* config.h.in.  Generated from configure.ac by autoheader.  */

/* Enable CUDA feature */
/* #undef CUDA_PATH */

/* Have AES XTS support */
#define HAVE_AES_XTS 1

/* Enable CUDA feature */
/* #undef HAVE_CUDA */

/* Enable CUDA DMABUF feature */
/* #undef HAVE_CUDA_DMABUF */

/* Have DCS support */
#define HAVE_DCS 1

/* Define to 1 if you have the <dlfcn.h> header file. */
#define HAVE_DLFCN_H 1

/* Enable endian conversion */
#define HAVE_ENDIAN 1

/* Have EX support */
#define HAVE_EX 1

/* Have Extended ODP support */
#define HAVE_EX_ODP 1

/* Have a way to check gid type */
#define HAVE_GID_TYPE 1

/* API GID compatibility */
#define HAVE_GID_TYPE_DECLARED 1

/* Define to 1 if you have the <hip/hip_runtime_api.h> header file. */
/* #undef HAVE_HIP_HIP_RUNTIME_API_H */

/* Enable Habana Labs benchmarks */
/* #undef HAVE_HL */

/* Define to 1 if you have the <hlthunk.h> header file. */
/* #undef HAVE_HLTHUNK_H */

/* Have new post send API support */
#define HAVE_IBV_WR_API 1

/* Define to 1 if you have the <infiniband/verbs.h> header file. */
#define HAVE_INFINIBAND_VERBS_H 1

/* Define to 1 if you have the <inttypes.h> header file. */
#define HAVE_INTTYPES_H 1

/* Enable IPv4 Extended Flow Specification */
#define HAVE_IPV4_EXT 1

/* Enable IPv6 Flow Specification */
#define HAVE_IPV6 1

/* Define to 1 if you have the `ibverbs' library (-libverbs). */
#define HAVE_LIBIBVERBS 1

/* Define to 1 if you have the `rdmacm' library (-lrdmacm). */
#define HAVE_LIBRDMACM 1

/* Define to 1 if you have the <misc/habanalabs.h> header file. */
/* #undef HAVE_MISC_HABANALABS_H */

/* Have Direct Verbs support */
#define HAVE_MLX5DV 1

/* Have MLX5 DEVX support */
#define HAVE_MLX5_DEVX 1

/* Enable Neuron benchmarks */
/* #undef HAVE_NEURON */

/* Enable Neuron DMA buffers */
/* #undef HAVE_NEURON_DMABUF */

/* Define to 1 if you have the <nrt/nrt.h> header file. */
/* #undef HAVE_NRT_NRT_H */

/* Have Out of order data placement support */
/* #undef HAVE_OOO_ATTR */

/* Have PACKET_PACING support */
#define HAVE_PACKET_PACING 1

/* Define to 1 if you have the <pci/pci.h> header file. */
#define HAVE_PCI_PCI_H 1

/* Enable RAW_ETH_TEST */
#define HAVE_RAW_ETH 1

/* Enable RAW_ETH_TEST_REG */
#define HAVE_RAW_ETH_REG 1

/* Enable HAVE_REG_DMABUF_MR */
#define HAVE_REG_DMABUF_MR 1

/* Enable Relaxed Ordering */
#define HAVE_RO 1

/* Enable ROCm */
/* #undef HAVE_ROCM */

/* Enable SCIF link Layer */
/* #undef HAVE_SCIF */

/* Enable Sniffer Flow Specification */
#define HAVE_SNIFFER 1

/* Have SRD support - Scalable Reliable Datagram*/
/* #define HAVE_SRD 1 */

/* Have SRD with RDMA read support */
#define HAVE_SRD_WITH_RDMA_READ 1

/* Have SRD with RDMA write support */
/* #undef HAVE_SRD_WITH_RDMA_WRITE */

/* Define to 1 if you have the <stdint.h> header file. */
#define HAVE_STDINT_H 1

/* Define to 1 if you have the <stdio.h> header file. */
#define HAVE_STDIO_H 1

/* Define to 1 if you have the <stdlib.h> header file. */
#define HAVE_STDLIB_H 1

/* Define to 1 if you have the <strings.h> header file. */
#define HAVE_STRINGS_H 1

/* Define to 1 if you have the <string.h> header file. */
#define HAVE_STRING_H 1

/* Define to 1 if you have the <synapse_api.h> header file. */
/* #undef HAVE_SYNAPSE_API_H */

/* Define to 1 if you have the <sys/stat.h> header file. */
#define HAVE_SYS_STAT_H 1

/* Define to 1 if you have the <sys/types.h> header file. */
#define HAVE_SYS_TYPES_H 1

/* Define to 1 if you have the <unistd.h> header file. */
#define HAVE_UNISTD_H 1

/* Enable XRCD feature */
#define HAVE_XRCD 1

/* OS is FreeBSD */
/* #undef IS_FREEBSD */

/* Define to the sub-directory where libtool stores uninstalled libraries. */
#define LT_OBJDIR ".libs/"

/* Name of package */
#define PACKAGE "perftest"

/* Define to the address where bug reports for this package should be sent. */
#define PACKAGE_BUGREPORT "linux-rdma@vger.kernel.org"

/* Define to the full name of this package. */
#define PACKAGE_NAME "perftest"

/* Define to the full name and version of this package. */
#define PACKAGE_STRING "perftest 6.20"

/* Define to the one symbol short name of this package. */
#define PACKAGE_TARNAME "perftest"

/* Define to the home page for this package. */
#define PACKAGE_URL ""

/* Define to the version of this package. */
#define PACKAGE_VERSION "6.20"

/* Define to 1 if all of the C90 standard headers exist (not just the ones
   required in a freestanding environment). This macro is provided for
   backward compatibility; new code need not use it. */
#define STDC_HEADERS 1

/* Version number of package */
#define VERSION "6.20"

/* Enable ROCm */
/* #undef __HIP_PLATFORM_HCC__ */
