/*
 *			GPAC - Multimedia Framework C SDK
 *
 *			Authors: Jean Le Feuvre
 *			Copyright (c) Telecom ParisTech 2000-2012
 *					All rights reserved
 *
 *  This file is part of GPAC / ISO Media File Format sub-project
 *
 *  GPAC is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  GPAC is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <gpac/internal/isomedia_dev.h>
#include <gpac/utf.h>
#include <gpac/network.h>
#include <gpac/color.h>
#include <gpac/avparse.h>
#include <time.h>

#ifndef GPAC_DISABLE_ISOM_DUMP

static GF_Err apple_tag_dump(GF_Box *a, FILE * trace);

void NullBoxErr(FILE * trace, u32 box_4cc)
{
	if (box_4cc) {
		fprintf(trace, "<!--ERROR: NULL Box Found, expecting %s -->\n", gf_4cc_to_str(box_4cc) );
	} else {
		fprintf(trace, "<!--ERROR: NULL Box Found-->\n");
	}
}

void BadTopBoxErr(GF_Box *a, FILE * trace)
{
	fprintf(trace, "<!--ERROR: Invalid Top-level Box Found (\"%s\")-->\n", gf_4cc_to_str(a->type));
}

static void DumpData(FILE *trace, char *data, u32 dataLength)
{
	u32 i;
	fprintf(trace, "data:application/octet-string,");
	for (i=0; i<dataLength; i++) {
		fprintf(trace, "%02X", (unsigned char) data[i]);
	}
}

static void DumpDataHex(FILE *trace, char *data, u32 dataLength)
{
	u32 i;
	fprintf(trace, "0x");
	for (i=0; i<dataLength; i++) {
		fprintf(trace, "%02X", (unsigned char) data[i]);
	}
}

static void DumpDataString(FILE *trace, char *data, u32 dataLength)
{
	u32 i;
	for (i=0; i<dataLength; i++) {
		switch ((unsigned char) data[i]) {
		case '\'':
			fprintf(trace, "&apos;");
			break;
		case '\"':
			fprintf(trace, "&quot;");
			break;
		case '&':
			fprintf(trace, "&amp;");
			break;
		case '>':
			fprintf(trace, "&gt;");
			break;
		case '<':
			fprintf(trace, "&lt;");
			break;
		default:
			fprintf(trace, "%c", (u8) data[i]);
			break;
		}
	}
}


GF_Err DumpBox(GF_Box *a, const char *name, FILE * trace)
{
	fprintf(trace, "<%s ", name);
	if (a->size > 0xFFFFFFFF) {
		fprintf(trace, "LargeSize=\""LLU"\" ", LLU_CAST a->size);
	} else {
		fprintf(trace, "Size=\"%u\" ", (u32) a->size);
	}
	if (a->type == GF_ISOM_BOX_TYPE_UUID) {
		u32 i;
		fprintf(trace, "UUID=\"{");
		for (i=0; i<16; i++) {
			fprintf(trace, "%02X", (unsigned char) ((GF_UUIDBox*)a)->uuid[i]);
			if ((i<15) && (i%4)==3) fprintf(trace, "-");
		}
		fprintf(trace, "}\" ");
	} else {
		fprintf(trace, "Type=\"%s\" ", gf_4cc_to_str(a->type));
	}
	return GF_OK;
}

static GF_Err uuid_ex_dump(GF_Box *a, FILE *trace)
{
	switch ( ((GF_UnknownUUIDBox *)a)->internal_4cc) {
	case GF_ISOM_BOX_UUID_TENC:
		return piff_tenc_dump(a, trace);
	case GF_ISOM_BOX_UUID_PSEC:
		return piff_psec_dump(a, trace);
	case GF_ISOM_BOX_UUID_PSSH:
		return piff_pssh_dump(a, trace);
	case GF_ISOM_BOX_UUID_TFXD:
		return tfxd_dump(a, trace);
	case GF_ISOM_BOX_UUID_MSSM:
	case GF_ISOM_BOX_UUID_TFRF:
	case GF_ISOM_BOX_TYPE_UNKNOWN:
		return uuid_dump(a, trace);
	default:
		return GF_ISOM_INVALID_FILE;
	}
}

#define BOX_DUMP_DEF(__type, __fun) { __type, __fun, 0, 0, 0 }

#define FBOX_DUMP_DEF(__type, __fun, __max_v) { __type, __fun, 0, __max_v, 0 }

#define TREF_DUMP_DEF(__type, __fun, __4cc) { __type, __fun, __4cc, 0, 0 }

#define TRGT_DUMP_DEF(__type, __fun, __4cc, max_version) { __type, __fun, 0, max_version, 0 }

static const struct box_def {
	u32 box_4cc;
	GF_Err (*dump_fn)(GF_Box *a, FILE *trace);
	u32 alt_4cc;//used for sample grouping type and track / item reference types
	u8 max_version;
	u32 flags;
} defined_box_types [] =
{
	BOX_DUMP_DEF(GF_ISOM_BOX_TYPE_UNKNOWN, unkn_dump),
	TREF_DUMP_DEF(GF_ISOM_BOX_TYPE_REFT, reftype_dump, GF_ISOM_REF_OD),
	TREF_DUMP_DEF(GF_ISOM_BOX_TYPE_REFT, reftype_dump, GF_ISOM_REF_DECODE),
	TREF_DUMP_DEF(GF_ISOM_BOX_TYPE_REFT, reftype_dump, GF_ISOM_REF_OCR),
	TREF_DUMP_DEF(GF_ISOM_BOX_TYPE_REFT, reftype_dump, GF_ISOM_REF_IPI),
	TREF_DUMP_DEF(GF_ISOM_BOX_TYPE_REFT, reftype_dump, GF_ISOM_REF_META),
	TREF_DUMP_DEF(GF_ISOM_BOX_TYPE_REFT, reftype_dump, GF_ISOM_REF_HINT),
	TREF_DUMP_DEF(GF_ISOM_BOX_TYPE_REFT, reftype_dump, GF_ISOM_REF_CHAP),
	TREF_DUMP_DEF(GF_ISOM_BOX_TYPE_REFT, reftype_dump, GF_ISOM_REF_BASE),
	TREF_DUMP_DEF(GF_ISOM_BOX_TYPE_REFT, reftype_dump, GF_ISOM_REF_SCAL),
	TREF_DUMP_DEF(GF_ISOM_BOX_TYPE_REFT, reftype_dump, GF_ISOM_REF_TBAS),
	TREF_DUMP_DEF(GF_ISOM_BOX_TYPE_REFT, reftype_dump, GF_ISOM_REF_SABT),
	TREF_DUMP_DEF(GF_ISOM_BOX_TYPE_REFT, reftype_dump, GF_ISOM_REF_OREF),
	TREF_DUMP_DEF(GF_ISOM_BOX_TYPE_REFT, reftype_dump, GF_ISOM_REF_FONT),
	TREF_DUMP_DEF(GF_ISOM_BOX_TYPE_REFT, reftype_dump, GF_ISOM_REF_HIND),
	TREF_DUMP_DEF(GF_ISOM_BOX_TYPE_REFT, reftype_dump, GF_ISOM_REF_VDEP),
	TREF_DUMP_DEF(GF_ISOM_BOX_TYPE_REFT, reftype_dump, GF_ISOM_REF_VPLX),
	TREF_DUMP_DEF(GF_ISOM_BOX_TYPE_REFT, reftype_dump, GF_ISOM_REF_SUBT),

	TREF_DUMP_DEF( GF_ISOM_BOX_TYPE_REFI, ireftype_dump, GF_ISOM_REF_TBAS),
	TREF_DUMP_DEF( GF_ISOM_BOX_TYPE_REFI, ireftype_dump, GF_4CC('i','l','o','c') ),

	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_FREE, free_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_SKIP, free_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_MDAT, mdat_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_MOOV, moov_dump),
	FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_MVHD, mvhd_dump, 1),
	FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_MDHD, mdhd_dump, 1),
	FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_VMHD, vmhd_dump, 0),
	FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_SMHD, smhd_dump, 0),
	FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_HMHD, hmhd_dump, 0),
	//the same box is used for all MPEG4 systems streams
	FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_ODHD, nmhd_dump, 0),
	FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_CRHD, nmhd_dump, 0),
	FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_SDHD, nmhd_dump, 0),
	FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_NMHD, nmhd_dump, 0),
	FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_STHD, nmhd_dump, 0),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_STBL, stbl_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_DINF, dinf_dump),
	FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_URL, url_dump, 0),
	FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_URN, urn_dump, 0),
	FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_CPRT, cprt_dump, 1),
	FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_KIND, kind_dump, 0),
	FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_HDLR, hdlr_dump, 0),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_IODS, iods_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_TRAK, trak_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_MP4S, mp4s_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_MP4V, mp4v_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_MP4A, mp4a_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_GNRM, gnrm_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_GNRV, gnrv_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_GNRA, gnra_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_EDTS, edts_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_UDTA, udta_dump),
	FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_DREF, dref_dump, 0),
	FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_STSD, stsd_dump, 0),
	FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_STTS, stts_dump, 0),
	FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_CTTS, ctts_dump, 1),
	FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_CSLG, cslg_dump, 1),
	FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_STSH, stsh_dump, 0),
	FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_ELST, elst_dump, 1),
	FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_STSC, stsc_dump, 0),
	FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_STZ2, stsz_dump, 0),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_STSZ, stsz_dump),
	FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_STCO, stco_dump, 0),
	FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_STSS, stss_dump, 0),
	FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_STDP, stdp_dump, 0),
	FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_SDTP, sdtp_dump, 0),
	FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_CO64, co64_dump, 0),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_ESDS, esds_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_MINF, minf_dump),
	FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_TKHD, tkhd_dump, 1),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_TREF, tref_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_MDIA, mdia_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_MFRA, mfra_dump),
	FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_TFRA, tfra_dump, 1),
	FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_ELNG, elng_dump, 0),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_CHPL, chpl_dump),
	FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_PDIN, dpin_dump, 0),
	FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_SBGP, sbgp_dump, 1),
	FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_SGPD, sgpd_dump, 2),

	TREF_DUMP_DEF( GF_ISOM_BOX_TYPE_SGPD, sgpd_dump, GF_ISOM_SAMPLE_GROUP_ROLL),
	TREF_DUMP_DEF( GF_ISOM_BOX_TYPE_SGPD, sgpd_dump, GF_ISOM_SAMPLE_GROUP_SEIG),
	TREF_DUMP_DEF( GF_ISOM_BOX_TYPE_SGPD, sgpd_dump, GF_ISOM_SAMPLE_GROUP_OINF),
	TREF_DUMP_DEF( GF_ISOM_BOX_TYPE_SGPD, sgpd_dump, GF_ISOM_SAMPLE_GROUP_LINF),
	TREF_DUMP_DEF( GF_ISOM_BOX_TYPE_SGPD, sgpd_dump, GF_ISOM_SAMPLE_GROUP_TRIF),
	TREF_DUMP_DEF( GF_ISOM_BOX_TYPE_SGPD, sgpd_dump, GF_ISOM_SAMPLE_GROUP_NALM),


	{ GF_ISOM_BOX_TYPE_SAIZ, saiz_dump, 0, 0, 0 },
	{ GF_ISOM_BOX_TYPE_SAIZ, saiz_dump, 0, 0, 1 },
	{ GF_ISOM_BOX_TYPE_SAIO, saio_dump, 0, 0, 0 },
	{ GF_ISOM_BOX_TYPE_SAIO, saio_dump, 0, 0, 1 },


	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_RTP_STSD, ghnt_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_RTPO, rtpo_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_HNTI, hnti_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_SDP, sdp_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_HINF, hinf_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_RELY, rely_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_TIMS, tims_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_TSRO, tsro_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_SNRO, snro_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_TRPY, trpy_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_NUMP, nump_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_TOTL, totl_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_NPCK, npck_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_TPYL, tpyl_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_TPAY, tpay_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_MAXR, maxr_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_DMED, dmed_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_DIMM, dimm_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_DREP, drep_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_TMIN, tmin_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_TMAX, tmax_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_PMAX, pmax_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_DMAX, dmax_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_PAYT, payt_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_NAME, name_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_FTYP, ftyp_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_STYP, ftyp_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_PADB, padb_dump),

#ifndef	GPAC_DISABLE_ISOM_FRAGMENTS
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_MVEX, mvex_dump),
	FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_MEHD, mehd_dump, 1),
	FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_TREX, trex_dump, 0),
	FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_TREP, trep_dump, 0),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_MOOF, moof_dump),
	FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_MFHD, mfhd_dump, 0),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_TRAF, traf_dump),
	//we only dump with all flags on
	{ GF_ISOM_BOX_TYPE_TFHD, tfhd_dump, 0, 0, 0x000001|0x000002|0x000008|0x000010|0x000020|0x010000|0x020000},
	{  GF_ISOM_BOX_TYPE_TRUN, trun_dump, 0, 0, 0x000001|0x000004|0x000100|0x000200|0x000400|0x000800 },

	FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_TFDT, tfdt_dump, 1),
#endif

	FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_SUBS, subs_dump, 1),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_RVCC, rvcc_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_TRGR, trgr_dump),

	TRGT_DUMP_DEF( GF_ISOM_BOX_TYPE_TRGT, trgt_dump, GF_4CC('m','s','r','c'), 0 ),

	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_VOID, void_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_STSF, stsf_dump),
	BOX_DUMP_DEF( GF_ISOM_SUBTYPE_3GP_AMR, gppa_dump),
	BOX_DUMP_DEF( GF_ISOM_SUBTYPE_3GP_AMR_WB, gppa_dump),
	BOX_DUMP_DEF( GF_ISOM_SUBTYPE_3GP_QCELP, gppa_dump),
	BOX_DUMP_DEF( GF_ISOM_SUBTYPE_3GP_EVRC, gppa_dump),
	BOX_DUMP_DEF( GF_ISOM_SUBTYPE_3GP_SMV, gppa_dump),
	BOX_DUMP_DEF( GF_ISOM_SUBTYPE_3GP_H263, gppv_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_DAMR, gppc_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_DEVC, gppc_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_DQCP, gppc_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_DSMV, gppc_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_D263, gppc_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_AVCC, avcc_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_SVCC, avcc_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_HVCC, hvcc_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_LHVC, hvcc_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_BTRT, btrt_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_M4DS, m4ds_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_AVC1, mp4v_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_AVC2, mp4v_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_AVC3, mp4v_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_AVC4, mp4v_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_SVC1, mp4v_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_HVC1, mp4v_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_HEV1, mp4v_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_HVC2, mp4v_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_HEV2, mp4v_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_LHV1, mp4v_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_LHE1, mp4v_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_HVT1, mp4v_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_PASP, pasp_dump),

	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_FTAB, ftab_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_TX3G, tx3g_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_TEXT, text_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_STYL, styl_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_HLIT, hlit_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_HCLR, hclr_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_KROK, krok_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_DLAY, dlay_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_HREF, href_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_TBOX, tbox_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_BLNK, blnk_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_TWRP, twrp_dump),

	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_PSSH, pssh_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_TENC, tenc_dump),

	/* ISMA 1.0 Encryption and Authentication V 1.0 */
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_IKMS, iKMS_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_ISFM, iSFM_dump),

	/*MPEG-21 extensions*/
	FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_META, meta_dump, 0),
	FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_XML, xml_dump, 0),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_BXML, bxml_dump),
	FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_ILOC, iloc_dump, 2),
	FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_PITM, pitm_dump, 1),
	FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_IPRO, ipro_dump, 0),
	FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_INFE, infe_dump, 1),
	FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_INFE, infe_dump, 2),

	FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_IINF, iinf_dump, 1),
	FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_IREF, iref_dump, 1),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_SINF, sinf_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_FRMA, frma_dump),

	{ GF_ISOM_BOX_TYPE_SCHM, schm_dump, 0, 0, 1 },

	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_SCHI, schi_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_ENCA, mp4a_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_ENCV, mp4v_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_ENCS, mp4s_dump),
	FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_PRFT, prft_dump, 1),

	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_0xA9NAM, apple_tag_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_0xA9CMT, apple_tag_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_0xA9DAY, apple_tag_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_0xA9ART, apple_tag_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_0xA9TRK, apple_tag_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_0xA9ALB, apple_tag_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_0xA9COM, apple_tag_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_0xA9WRT, apple_tag_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_0xA9TOO, apple_tag_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_0xA9CPY, apple_tag_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_0xA9DES, apple_tag_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_0xA9GEN, apple_tag_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_0xA9GRP, apple_tag_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_GNRE, apple_tag_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_DISK, apple_tag_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_TRKN, apple_tag_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_TMPO, apple_tag_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_CPIL, apple_tag_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_COVR, apple_tag_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_iTunesSpecificInfo, apple_tag_dump),
#ifndef GPAC_DISABLE_ISOM_ADOBE
	/*Adobe extensions*/
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_ABST, abst_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_AFRA, afra_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_ASRT, asrt_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_AFRT, afrt_dump),
#endif
	/*Apple extensions*/
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_ILST, ilst_dump),

	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_OHDR, ohdr_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_GRPI, grpi_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_MDRI, mdri_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_ODTT, odtt_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_ODRB, odrb_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_ODKM, odkm_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_ODAF, iSFM_dump),

	FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_TSEL, tsel_dump, 0),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_STRK, strk_dump),
	FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_STRI, stri_dump, 0),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_METX, metx_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_METT, metx_dump),

	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_DIMS, dims_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_DIMC, dimC_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_DIST, diST_dump),

	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_AC3, ac3_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_DAC3, dac3_dump),

	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_LSR1, lsr1_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_LSRC, lsrc_dump),
	FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_SIDX, sidx_dump, 1),
	FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_SSIX, ssix_dump, 0),
	FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_LEVA, leva_dump, 0),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_PCRB, pcrb_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_SENC, senc_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_UUID, uuid_ex_dump),

#ifndef GPAC_DISABLE_TTXT
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_STXT, metx_dump),
	FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_TXTC, txtc_dump, 0),
#ifndef GPAC_DISABLE_VTT
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_VTTC, boxstring_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_CTIM, boxstring_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_IDEN, boxstring_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_STTG, boxstring_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_PAYL, boxstring_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_VTTA, boxstring_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_VTCU, vtcu_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_VTTE, vtte_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_WVTT, wvtt_dump),
#endif
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_STPP, metx_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_SBTT, metx_dump),
#endif

	/*Adobe's protection boxes*/
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_ADKM, adkm_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_AHDR, ahdr_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_ADAF, adaf_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_APRM, aprm_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_AEIB, aeib_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_AKEY, akey_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_FLXS, flxs_dump),

	/* Image File Format */
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_ISPE, ispe_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_COLR, colr_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_PIXI, pixi_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_RLOC, rloc_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_IROT, irot_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_IPCO, ipco_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_IPRP, iprp_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_IPMA, ipma_dump),
	BOX_DUMP_DEF( GF_ISOM_BOX_TYPE_GRPL, grpl_dump)

	//full boxes todo
	//FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_MFRO, mfro_dump, 0),
	//FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_ASSP, assp_dump, 1),
	//FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_MERE, assp_dump, 0),
	//FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_FIIN, fiin_dump, 0),
	//FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_FPAR, fpar_dump, 1),
	//FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_FECR, fecr_dump, 1),
	//FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_GITN, gitn_dump, 0),
	//FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_FIRE, fire_dump, 1),
	//FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_STSG, stsg_dump, 0),
	//FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_STVI, stvi_dump, 0),
	//FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_SRPP, srpp_dump, 0),
	//FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_SRAT, srat_dump, 0),
	//FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_CHNL, chnl_dump, 0),
	//FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_DMIX, dmix_dump, 0),
	//FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_TLOU, alou_dump, 0),
	//FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_ALOU, alou_dump, 0),
	//FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_URI, uri_dump, 0),
	//FBOX_DUMP_DEF( GF_ISOM_BOX_TYPE_URII, urii_dump, 0),

};

GF_EXPORT
u32 gf_isom_get_num_supported_boxes()
{
	return sizeof(defined_box_types) / sizeof(struct box_def);
}

GF_EXPORT
GF_Err gf_isom_dump_supported_box(u32 idx, FILE * trace)
{
	GF_Err e;
	GF_Box *a = gf_isom_box_new( defined_box_types[idx].box_4cc);
	if (defined_box_types[idx].alt_4cc) {
		if (a->type==GF_ISOM_BOX_TYPE_REFT) ((GF_TrackReferenceTypeBox*)a)->reference_type = defined_box_types[idx].alt_4cc;
		else if (a->type==GF_ISOM_BOX_TYPE_REFI) ((GF_ItemReferenceTypeBox*)a)->reference_type = defined_box_types[idx].alt_4cc;
		else if (a->type==GF_ISOM_BOX_TYPE_TRGT) ((GF_TrackGroupTypeBox*)a)->group_type = defined_box_types[idx].alt_4cc;
		else if (a->type==GF_ISOM_BOX_TYPE_SGPD)
			((GF_SampleGroupDescriptionBox*)a)->grouping_type = defined_box_types[idx].alt_4cc;
	}
	if (defined_box_types[idx].max_version) {
		((GF_FullBox *)a)->version = defined_box_types[idx].max_version;
	}
	if (defined_box_types[idx].flags) {
		((GF_FullBox *)a)->flags = defined_box_types[idx].flags;
	}
	e = gf_box_dump(a, trace);
	gf_isom_box_del(a);
	return e;
}

GF_EXPORT
u32 gf_isom_get_supported_box_type(u32 idx)
{
	return defined_box_types[idx].box_4cc;
}


GF_Err gf_box_dump_ex(void *ptr, FILE * trace, u32 box_4cc)
{
	u32 i, count = gf_isom_get_num_supported_boxes();
	GF_Box *a = (GF_Box *) ptr;

	if (!a) {
		NullBoxErr(trace, box_4cc);
		return GF_OK;
	}
	for (i=0; i<count; i++) {
		if (defined_box_types[i].box_4cc == a->type) {
			return defined_box_types[i].dump_fn(a, trace);
		}
	}
	GF_LOG(GF_LOG_ERROR, GF_LOG_CONTAINER, ("[isom] trying to dump box %s not registered\n", gf_4cc_to_str(a->type) ));
	return GF_ISOM_INVALID_FILE;
}


GF_Err gf_box_dump(void *ptr, FILE * trace)
{
	return gf_box_dump_ex(ptr, trace, 0);
}

GF_Err gf_box_array_dump(GF_List *list, FILE * trace)
{
	u32 i;
	GF_Box *a;
	if (!list) return GF_OK;
	i=0;
	while ((a = (GF_Box *)gf_list_enum(list, &i))) {
		gf_box_dump(a, trace);
	}
	return GF_OK;
}

void gf_box_dump_done(char *name, GF_Box *ptr, FILE *trace)
{
	if (ptr && ptr->other_boxes) {
		gf_box_array_dump(ptr->other_boxes, trace);
	}
	if (name)
		fprintf(trace, "</%s>\n", name);
}


GF_EXPORT
GF_Err gf_isom_dump(GF_ISOFile *mov, FILE * trace)
{
	u32 i;
	GF_Box *box;
	if (!mov || !trace) return GF_BAD_PARAM;

	fprintf(trace, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
	fprintf(trace, "<!--MP4Box dump trace-->\n");

	fprintf(trace, "<IsoMediaFile xmlns=\"urn:mpeg:isobmff:schema:file:2016\" Name=\"%s\">\n", mov->fileName);

	i=0;
	while ((box = (GF_Box *)gf_list_enum(mov->TopBoxes, &i))) {
		switch (box->type) {
		case GF_ISOM_BOX_TYPE_FTYP:
		case GF_ISOM_BOX_TYPE_MOOV:
		case GF_ISOM_BOX_TYPE_MDAT:
		case GF_ISOM_BOX_TYPE_FREE:
		case GF_ISOM_BOX_TYPE_META:
		case GF_ISOM_BOX_TYPE_SKIP:
#ifndef	GPAC_DISABLE_ISOM_FRAGMENTS
		case GF_ISOM_BOX_TYPE_MOOF:
		case GF_ISOM_BOX_TYPE_STYP:
		case GF_ISOM_BOX_TYPE_SIDX:
		case GF_ISOM_BOX_TYPE_SSIX:
		case GF_ISOM_BOX_TYPE_PCRB:
#ifndef GPAC_DISABLE_ISOM_ADOBE
		/*Adobe specific*/
		case GF_ISOM_BOX_TYPE_AFRA:
		case GF_ISOM_BOX_TYPE_ABST:
#endif
#endif
		case GF_ISOM_BOX_TYPE_MFRA:
		case GF_ISOM_BOX_TYPE_PRFT:
		case GF_ISOM_BOX_TYPE_UUID:
			break;

		default:
			BadTopBoxErr(box, trace);
			break;
		}
		gf_box_dump(box, trace);
	}
	fprintf(trace, "</IsoMediaFile>\n");
	return GF_OK;
}



GF_Err gf_full_box_dump(GF_Box *a, FILE * trace)
{
	GF_FullBox *p;
	p = (GF_FullBox *)a;
	fprintf(trace, "Version=\"%d\" Flags=\"0x%X\" ", p->version, p->flags);
	return GF_OK;
}


GF_Err reftype_dump(GF_Box *a, FILE * trace)
{
	u32 i;
	GF_TrackReferenceTypeBox *p = (GF_TrackReferenceTypeBox *)a;
	if (!p->reference_type) return GF_OK;
	p->type = p->reference_type;

	DumpBox(a, "TrackReferenceTypeBox", trace);
	fprintf(trace, "Tracks=\"");
	for (i=0; i<p->trackIDCount; i++) fprintf(trace, " %d", p->trackIDs[i]);
	fprintf(trace, "\">\n");

	gf_box_dump_done("TrackReferenceTypeBox", a, trace);
	p->type = GF_ISOM_BOX_TYPE_REFT;
	return GF_OK;
}

GF_Err ireftype_dump(GF_Box *a, FILE * trace)
{
	const char *s;
	u32 i;
	char boxname[256];
	GF_ItemReferenceTypeBox *p = (GF_ItemReferenceTypeBox *)a;
	if (!p->reference_type) return GF_OK;

	p->type = p->reference_type;
	
	s = a->type ? gf_4cc_to_str(a->type) : "";
	sprintf(boxname, "%sItemReferenceBox", s);
	DumpBox(a, boxname, trace);
	fprintf(trace, "from_item_id=\"%d\" to_item_ids=\"", p->from_item_id);
	for (i = 0; i<p->reference_count; i++) fprintf(trace, " %d", p->to_item_IDs[i]);
	fprintf(trace, "\">\n");

	gf_box_dump_done(boxname, a, trace);

	p->type = GF_ISOM_BOX_TYPE_REFI;
	return GF_OK;
}

GF_Err free_dump(GF_Box *a, FILE * trace)
{
	GF_FreeSpaceBox *p = (GF_FreeSpaceBox *)a;
	DumpBox(a, (a->type==GF_ISOM_BOX_TYPE_FREE) ? "FreeSpaceBox" : "SkipBox", trace);
	fprintf(trace, "dataSize=\"%d\"/>\n", p->dataSize);
	//gf_box_dump_done("FreeSpaceBox", a, trace);
	return GF_OK;
}

GF_Err mdat_dump(GF_Box *a, FILE * trace)
{
	GF_MediaDataBox *p;

	p = (GF_MediaDataBox *)a;
	DumpBox(a, "MediaDataBox", trace);
	fprintf(trace, "dataSize=\""LLD"\"/>\n", LLD_CAST p->dataSize);
	//gf_box_dump_done("MediaDataBox", a, trace);
	return GF_OK;
}

GF_Err moov_dump(GF_Box *a, FILE * trace)
{
	GF_MovieBox *p;
	p = (GF_MovieBox *)a;
	DumpBox(a, "MovieBox", trace);
	fprintf(trace, ">\n");
	if (p->iods) gf_box_dump(p->iods, trace);
	if (p->meta) gf_box_dump(p->meta, trace);
	//dump only if size
	if (p->size)
		gf_box_dump_ex(p->mvhd, trace,GF_ISOM_BOX_TYPE_MVHD);

#ifndef	GPAC_DISABLE_ISOM_FRAGMENTS
	if (p->mvex) gf_box_dump(p->mvex, trace);
#endif

	gf_box_array_dump(p->trackList, trace);
	if (p->udta) gf_box_dump(p->udta, trace);
	gf_box_dump_done("MovieBox", a, trace);
	return GF_OK;
}

GF_Err mvhd_dump(GF_Box *a, FILE * trace)
{
	GF_MovieHeaderBox *p;

	p = (GF_MovieHeaderBox *) a;

	DumpBox(a, "MovieHeaderBox", trace);
	gf_full_box_dump(a, trace);
	fprintf(trace, "CreationTime=\""LLD"\" ", LLD_CAST p->creationTime);
	fprintf(trace, "ModificationTime=\""LLD"\" ", LLD_CAST p->modificationTime);
	fprintf(trace, "TimeScale=\"%d\" ", p->timeScale);
	fprintf(trace, "Duration=\""LLD"\" ", LLD_CAST p->duration);
	fprintf(trace, "NextTrackID=\"%d\">\n", p->nextTrackID);

	gf_box_dump_done("MovieHeaderBox", a, trace);
	return GF_OK;
}

GF_Err mdhd_dump(GF_Box *a, FILE * trace)
{
	GF_MediaHeaderBox *p;

	p = (GF_MediaHeaderBox *)a;
	DumpBox(a, "MediaHeaderBox", trace);
	gf_full_box_dump(a, trace);
	fprintf(trace, "CreationTime=\""LLD"\" ", LLD_CAST p->creationTime);
	fprintf(trace, "ModificationTime=\""LLD"\" ", LLD_CAST p->modificationTime);
	fprintf(trace, "TimeScale=\"%d\" ", p->timeScale);
	fprintf(trace, "Duration=\""LLD"\" ", LLD_CAST p->duration);
	fprintf(trace, "LanguageCode=\"%c%c%c\">\n", p->packedLanguage[0], p->packedLanguage[1], p->packedLanguage[2]);
	gf_box_dump_done("MediaHeaderBox", a, trace);
	return GF_OK;
}

GF_Err vmhd_dump(GF_Box *a, FILE * trace)
{
	DumpBox(a, "VideoMediaHeaderBox", trace);
	gf_full_box_dump(a, trace);
	fprintf(trace, ">\n");
	gf_box_dump_done("VideoMediaHeaderBox", a, trace);
	return GF_OK;
}

GF_Err smhd_dump(GF_Box *a, FILE * trace)
{
	DumpBox(a, "SoundMediaHeaderBox", trace);
	gf_full_box_dump(a, trace);
	fprintf(trace, ">\n");
	gf_box_dump_done("SoundMediaHeaderBox", a, trace);
	return GF_OK;
}

GF_Err hmhd_dump(GF_Box *a, FILE * trace)
{
	GF_HintMediaHeaderBox *p;

	p = (GF_HintMediaHeaderBox *)a;

	DumpBox(a, "HintMediaHeaderBox", trace);
	gf_full_box_dump(a, trace);
	fprintf(trace, "MaximumPDUSize=\"%d\" ", p->maxPDUSize);
	fprintf(trace, "AveragePDUSize=\"%d\" ", p->avgPDUSize);
	fprintf(trace, "MaxBitRate=\"%d\" ", p->maxBitrate);
	fprintf(trace, "AverageBitRate=\"%d\">\n", p->avgBitrate);

	gf_box_dump_done("HintMediaHeaderBox", a, trace);
	return GF_OK;
}

GF_Err nmhd_dump(GF_Box *a, FILE * trace)
{
	DumpBox(a, "MPEGMediaHeaderBox", trace);
	gf_full_box_dump(a, trace);
	fprintf(trace, ">\n");
	gf_box_dump_done("MPEGMediaHeaderBox", a, trace);
	return GF_OK;
}

GF_Err stbl_dump(GF_Box *a, FILE * trace)
{
	GF_SampleTableBox *p;
	p = (GF_SampleTableBox *)a;
	DumpBox(a, "SampleTableBox", trace);
	fprintf(trace, ">\n");

	if (p->size)
		gf_box_dump_ex(p->SampleDescription, trace, GF_ISOM_BOX_TYPE_STSD);
	if (p->size)
		gf_box_dump_ex(p->TimeToSample, trace, GF_ISOM_BOX_TYPE_STTS);

	if (p->CompositionOffset) gf_box_dump(p->CompositionOffset, trace);
	if (p->CompositionToDecode) gf_box_dump(p->CompositionToDecode, trace);
	if (p->SyncSample) gf_box_dump(p->SyncSample, trace);
	if (p->ShadowSync) gf_box_dump(p->ShadowSync, trace);

	if (p->size)
		gf_box_dump_ex(p->SampleToChunk, trace, GF_ISOM_BOX_TYPE_STSC);
	if (p->size)
		gf_box_dump_ex(p->SampleSize, trace, GF_ISOM_BOX_TYPE_STSZ);
	if (p->size)
		gf_box_dump_ex(p->ChunkOffset, trace, GF_ISOM_BOX_TYPE_STCO);

	if (p->DegradationPriority) gf_box_dump(p->DegradationPriority, trace);
	if (p->SampleDep) gf_box_dump(p->SampleDep, trace);
	if (p->PaddingBits) gf_box_dump(p->PaddingBits, trace);
	if (p->Fragments) gf_box_dump(p->Fragments, trace);
	if (p->sub_samples) gf_box_array_dump(p->sub_samples, trace);
	if (p->sampleGroupsDescription) gf_box_array_dump(p->sampleGroupsDescription, trace);
	if (p->sampleGroups) gf_box_array_dump(p->sampleGroups, trace);
	if (p->sai_sizes) {
		u32 i;
		for (i = 0; i < gf_list_count(p->sai_sizes); i++) {
			GF_SampleAuxiliaryInfoSizeBox *saiz = (GF_SampleAuxiliaryInfoSizeBox *)gf_list_get(p->sai_sizes, i);
			gf_box_dump(saiz, trace);
		}
	}

	if (p->sai_offsets) {
		u32 i;
		for (i = 0; i < gf_list_count(p->sai_offsets); i++) {
			GF_SampleAuxiliaryInfoOffsetBox *saio = (GF_SampleAuxiliaryInfoOffsetBox *)gf_list_get(p->sai_offsets, i);
			gf_box_dump(saio, trace);
		}
	}

	gf_box_dump_done("SampleTableBox", a, trace);
	return GF_OK;
}

GF_Err dinf_dump(GF_Box *a, FILE * trace)
{
	GF_DataInformationBox *p;
	p = (GF_DataInformationBox *)a;
	DumpBox(a, "DataInformationBox", trace);
	fprintf(trace, ">\n");
	if (p->size)
		gf_box_dump_ex(p->dref, trace, GF_ISOM_BOX_TYPE_DREF);

	gf_box_dump_done("DataInformationBox", a, trace);
	return GF_OK;
}

GF_Err url_dump(GF_Box *a, FILE * trace)
{
	GF_DataEntryURLBox *p;

	p = (GF_DataEntryURLBox *)a;
	DumpBox(a, "URLDataEntryBox", trace);
	gf_full_box_dump(a, trace);
	if (p->location) {
		fprintf(trace, " URL=\"%s\">\n", p->location);
	} else {
		fprintf(trace, ">\n");
		if (p->size) {
			if (! (p->flags & 1) ) {
				fprintf(trace, "<!--ERROR: No location indicated-->\n");
			} else {
				fprintf(trace, "<!--Data is contained in the movie file-->\n");
			}
		}
	}
	gf_box_dump_done("URLDataEntryBox", a, trace);
	return GF_OK;
}

GF_Err urn_dump(GF_Box *a, FILE * trace)
{
	GF_DataEntryURNBox *p;

	p = (GF_DataEntryURNBox *)a;
	DumpBox(a, "URNDataEntryBox", trace);
	gf_full_box_dump(a, trace);
	if (p->nameURN) fprintf(trace, " URN=\"%s\"", p->nameURN);
	if (p->location) fprintf(trace, " URL=\"%s\"", p->location);
	fprintf(trace, ">\n");

	gf_box_dump_done("URNDataEntryBox", a, trace);
	return GF_OK;
}

GF_Err cprt_dump(GF_Box *a, FILE * trace)
{
	GF_CopyrightBox *p;

	p = (GF_CopyrightBox *)a;
	DumpBox(a, "CopyrightBox", trace);
	gf_full_box_dump(a, trace);
	fprintf(trace, "LanguageCode=\"%s\" CopyrightNotice=\"%s\">\n", p->packedLanguageCode, p->notice);
	gf_box_dump_done("CopyrightBox", a, trace);
	return GF_OK;
}

GF_Err kind_dump(GF_Box *a, FILE * trace)
{
	GF_KindBox *p;

	p = (GF_KindBox *)a;
	DumpBox(a, "KindBox", trace);
	gf_full_box_dump(a, trace);
	fprintf(trace, "schemeURI=\"%s\" value=\"%s\">\n", p->schemeURI, (p->value ? p->value : ""));
	gf_box_dump_done("KindBox", a, trace);
	return GF_OK;
}


static char *format_duration(u64 dur, u32 timescale, char *szDur)
{
	u32 h, m, s, ms;
	dur = (u32) (( ((Double) (s64) dur)/timescale)*1000);
	h = (u32) (dur / 3600000);
	dur -= h*3600000;
	m = (u32) (dur / 60000);
	dur -= m*60000;
	s = (u32) (dur/1000);
	dur -= s*1000;
	ms = (u32) (dur);
	sprintf(szDur, "%02d:%02d:%02d.%03d", h, m, s, ms);
	return szDur;
}

static void dump_escape_string(FILE * trace, char *name)
{
	u32 i, len = (u32) strlen(name);
	for (i=0; i<len; i++) {
		if (name[i]=='"') fprintf(trace, "&quot;");
		else fputc(name[i], trace);
	}
}

GF_Err chpl_dump(GF_Box *a, FILE * trace)
{
	u32 i, count;
	char szDur[20];
	GF_ChapterListBox *p = (GF_ChapterListBox *)a;
	DumpBox(a, "ChapterListBox", trace);
	gf_full_box_dump(a, trace);
	fprintf(trace, ">\n");

	if (p->size) {
		count = gf_list_count(p->list);
		for (i=0; i<count; i++) {
			GF_ChapterEntry *ce = (GF_ChapterEntry *)gf_list_get(p->list, i);
			fprintf(trace, "<Chapter name=\"");
			dump_escape_string(trace, ce->name);
			fprintf(trace, "\" startTime=\"%s\" />\n", format_duration(ce->start_time, 1000*10000, szDur));
		}
	} else {
		fprintf(trace, "<Chapter name=\"\" startTime=\"\"/>\n");
	}

	gf_box_dump_done("ChapterListBox", a, trace);
	return GF_OK;
}

GF_Err dpin_dump(GF_Box *a, FILE * trace)
{
	u32 i;
	GF_ProgressiveDownloadBox *p = (GF_ProgressiveDownloadBox *)a;
	DumpBox(a, "ProgressiveDownloadBox", trace);
	gf_full_box_dump(a, trace);
	fprintf(trace, ">\n");

	if (p->size) {
		for (i=0; i<p->count; i++) {
			fprintf(trace, "<DownloadInfo rate=\"%d\" estimatedTime=\"%d\" />\n", p->rates[i], p->times[i]);
		}
	} else {
		fprintf(trace, "<DownloadInfo rate=\"\" estimatedTime=\"\" />\n");
	}
	gf_box_dump_done("ProgressiveDownloadBox", a, trace);
	return GF_OK;
}

GF_Err hdlr_dump(GF_Box *a, FILE * trace)
{
	GF_HandlerBox *p = (GF_HandlerBox *)a;
	DumpBox(a, "HandlerBox", trace);
	gf_full_box_dump(a, trace);
	if (p->nameUTF8 && (u32) p->nameUTF8[0] == strlen(p->nameUTF8+1)) {
		fprintf(trace, "hdlrType=\"%s\" Name=\"%s\" ", gf_4cc_to_str(p->handlerType), p->nameUTF8+1);
	} else {
		fprintf(trace, "hdlrType=\"%s\" Name=\"%s\" ", gf_4cc_to_str(p->handlerType), p->nameUTF8);
	}
	fprintf(trace, "reserved1=\"%d\" reserved2=\"", p->reserved1);
	DumpData(trace, (char *) p->reserved2, 12);
	fprintf(trace, "\"");

	fprintf(trace, ">\n");
	gf_box_dump_done("HandlerBox", a, trace);
	return GF_OK;
}

GF_Err iods_dump(GF_Box *a, FILE * trace)
{
	GF_ObjectDescriptorBox *p;

	p = (GF_ObjectDescriptorBox *)a;
	DumpBox(a, "ObjectDescriptorBox", trace);
	gf_full_box_dump(a, trace);
	fprintf(trace, ">\n");

	if (p->descriptor) {
#ifndef GPAC_DISABLE_OD_DUMP
		gf_odf_dump_desc(p->descriptor, trace, 1, GF_TRUE);
#else
		fprintf(trace, "<!-- Object Descriptor Dumping disabled in this build of GPAC -->\n");
#endif
	} else if (p->size) {
		fprintf(trace, "<!--WARNING: Object Descriptor not present-->\n");
	}
	gf_box_dump_done("ObjectDescriptorBox", a, trace);
	return GF_OK;
}

GF_Err trak_dump(GF_Box *a, FILE * trace)
{
	GF_TrackBox *p;

	p = (GF_TrackBox *)a;
	DumpBox(a, "TrackBox", trace);
	fprintf(trace, ">\n");
	if (p->Header) {
		gf_box_dump(p->Header, trace);
	} else if (p->size) {
		fprintf(trace, "<!--INVALID FILE: Missing Track Header-->\n");
	}
	if (p->References) gf_box_dump(p->References, trace);
	if (p->meta) gf_box_dump(p->meta, trace);
	if (p->editBox) gf_box_dump(p->editBox, trace);
	if (p->Media) gf_box_dump(p->Media, trace);
	if (p->groups) gf_box_dump(p->groups, trace);
	if (p->udta) gf_box_dump(p->udta, trace);
	gf_box_dump_done("TrackBox", a, trace);
	return GF_OK;
}

GF_Err mp4s_dump(GF_Box *a, FILE * trace)
{
	GF_MPEGSampleEntryBox *p;

	p = (GF_MPEGSampleEntryBox *)a;
	DumpBox(a, "MPEGSystemsSampleDescriptionBox", trace);
	fprintf(trace, "DataReferenceIndex=\"%d\">\n", p->dataReferenceIndex);
	if (p->esd) {
		gf_box_dump(p->esd, trace);
	} else if (p->size) {
		fprintf(trace, "<!--INVALID MP4 FILE: ESDBox not present in MPEG Sample Description or corrupted-->\n");
	}
	if (a->type == GF_ISOM_BOX_TYPE_ENCS) {
		gf_box_array_dump(p->protections, trace);
	}
	gf_box_dump_done("MPEGSystemsSampleDescriptionBox", a, trace);
	return GF_OK;
}


void base_visual_entry_dump(GF_VisualSampleEntryBox *p, FILE * trace)
{
	fprintf(trace, " DataReferenceIndex=\"%d\" Width=\"%d\" Height=\"%d\"", p->dataReferenceIndex, p->Width, p->Height);

	//dump reserved info
	fprintf(trace, " XDPI=\"%d\" YDPI=\"%d\" BitDepth=\"%d\"", p->horiz_res, p->vert_res, p->bit_depth);
	if (strlen((const char*)p->compressor_name) )
		fprintf(trace, " CompressorName=\"%s\"\n", p->compressor_name+1);

}

GF_Err mp4v_dump(GF_Box *a, FILE * trace)
{
	GF_MPEGVisualSampleEntryBox *p = (GF_MPEGVisualSampleEntryBox *)a;
	const char *name = p->avc_config ? "AVCSampleEntryBox" : "MPEGVisualSampleDescriptionBox";

	DumpBox(a, name, trace);
	base_visual_entry_dump((GF_VisualSampleEntryBox *)p, trace);
	fprintf(trace, ">\n");

	if (p->esd) {
		gf_box_dump(p->esd, trace);
	} else {
		if (p->hevc_config) gf_box_dump(p->hevc_config, trace);
		if (p->avc_config) gf_box_dump(p->avc_config, trace);
		if (p->ipod_ext) gf_box_dump(p->ipod_ext, trace);
		if (p->descr) gf_box_dump(p->descr, trace);
		if (p->svc_config) gf_box_dump(p->svc_config, trace);
		if (p->lhvc_config) gf_box_dump(p->lhvc_config, trace);
	}
	if (a->type == GF_ISOM_BOX_TYPE_ENCV) {
		gf_box_array_dump(p->protections, trace);
	}
	if (p->pasp) gf_box_dump(p->pasp, trace);
	if (p->rvcc) gf_box_dump(p->rvcc, trace);

	gf_box_dump_done((char *)name, a, trace);
	return GF_OK;
}


void base_audio_entry_dump(GF_AudioSampleEntryBox *p, FILE * trace)
{
	fprintf(trace, " DataReferenceIndex=\"%d\" SampleRate=\"%d\"", p->dataReferenceIndex, p->samplerate_hi);
	fprintf(trace, " Channels=\"%d\" BitsPerSample=\"%d\"", p->channel_count, p->bitspersample);
}

GF_Err mp4a_dump(GF_Box *a, FILE * trace)
{
	GF_MPEGAudioSampleEntryBox *p;

	p = (GF_MPEGAudioSampleEntryBox *)a;
	DumpBox(a, "MPEGAudioSampleDescriptionBox", trace);
	base_audio_entry_dump((GF_AudioSampleEntryBox *)p, trace);
	fprintf(trace, ">\n");

	if (p->esd) {
		gf_box_dump(p->esd, trace);
	} else if (p->size) {
		fprintf(trace, "<!--INVALID MP4 FILE: ESDBox not present in MPEG Sample Description or corrupted-->\n");
	}
	if (a->type == GF_ISOM_BOX_TYPE_ENCA) {
		gf_box_array_dump(p->protections, trace);
	}
	gf_box_dump_done("MPEGAudioSampleDescriptionBox", a, trace);
	return GF_OK;
}

GF_Err gnrm_dump(GF_Box *a, FILE * trace)
{
	GF_GenericSampleEntryBox *p = (GF_GenericSampleEntryBox *)a;
	DumpBox(a, "SampleDescriptionBox", trace);
	fprintf(trace, "DataReferenceIndex=\"%d\" ExtensionDataSize=\"%d\">\n", p->dataReferenceIndex, p->data_size);
	a->type = p->EntryType;
	a->type = GF_ISOM_BOX_TYPE_GNRM;
	gf_box_dump_done("SampleDescriptionBox", a, trace);
	return GF_OK;
}

GF_Err gnrv_dump(GF_Box *a, FILE * trace)
{
	GF_GenericVisualSampleEntryBox *p = (GF_GenericVisualSampleEntryBox *)a;
	DumpBox(a, "VisualSampleDescriptionBox", trace);
	fprintf(trace, "DataReferenceIndex=\"%d\" Version=\"%d\" Revision=\"%d\" Vendor=\"%d\" TemporalQuality=\"%d\" SpacialQuality=\"%d\" Width=\"%d\" Height=\"%d\" HorizontalResolution=\"%d\" VerticalResolution=\"%d\" CompressorName=\"%s\" BitDepth=\"%d\">\n",
	        p->dataReferenceIndex, p->version, p->revision, p->vendor, p->temporal_quality, p->spatial_quality, p->Width, p->Height, p->horiz_res, p->vert_res, p->compressor_name+1, p->bit_depth);
	a->type = p->EntryType;
	a->type = GF_ISOM_BOX_TYPE_GNRV;
	gf_box_dump_done("VisualSampleDescriptionBox", a, trace);
	return GF_OK;
}

GF_Err gnra_dump(GF_Box *a, FILE * trace)
{
	GF_GenericAudioSampleEntryBox *p = (GF_GenericAudioSampleEntryBox *)a;
	DumpBox(a, "AudioSampleDescriptionBox", trace);
	fprintf(trace, "DataReferenceIndex=\"%d\" Version=\"%d\" Revision=\"%d\" Vendor=\"%d\" ChannelCount=\"%d\" BitsPerSample=\"%d\" Samplerate=\"%d\">\n",
	        p->dataReferenceIndex, p->version, p->revision, p->vendor, p->channel_count, p->bitspersample, p->samplerate_hi);
	a->type = p->EntryType;
	a->type = GF_ISOM_BOX_TYPE_GNRA;
	gf_box_dump_done("AudioSampleDescriptionBox", a, trace);
	return GF_OK;
}

GF_Err edts_dump(GF_Box *a, FILE * trace)
{
	GF_EditBox *p;

	p = (GF_EditBox *)a;
	DumpBox(a, "EditBox", trace);
	fprintf(trace, ">\n");
	if (p->size)
		gf_box_dump_ex(p->editList, trace, GF_ISOM_BOX_TYPE_ELST);
	gf_box_dump_done("EditBox", a, trace);
	return GF_OK;
}

GF_Err udta_dump(GF_Box *a, FILE * trace)
{
	GF_UserDataBox *p;
	GF_UserDataMap *map;
	u32 i;

	p = (GF_UserDataBox *)a;
	DumpBox(a, "UserDataBox", trace);
	fprintf(trace, ">\n");

	i=0;
	while ((map = (GF_UserDataMap *)gf_list_enum(p->recordList, &i))) {
		gf_box_array_dump(map->other_boxes, trace);
	}
	gf_box_dump_done("UserDataBox", a, trace);
	return GF_OK;
}

GF_Err dref_dump(GF_Box *a, FILE * trace)
{
//	GF_DataReferenceBox *p = (GF_DataReferenceBox *)a;
	DumpBox(a, "DataReferenceBox", trace);
	gf_full_box_dump(a, trace);
	fprintf(trace, ">\n");
	gf_box_dump_done("DataReferenceBox", a, trace);
	return GF_OK;
}

GF_Err stsd_dump(GF_Box *a, FILE * trace)
{
//	GF_SampleDescriptionBox *p = (GF_SampleDescriptionBox *)a;
	DumpBox(a, "SampleDescriptionBox", trace);
	gf_full_box_dump(a, trace);
	fprintf(trace, ">\n");
	gf_box_dump_done("SampleDescriptionBox", a, trace);
	return GF_OK;
}

GF_Err stts_dump(GF_Box *a, FILE * trace)
{
	GF_TimeToSampleBox *p;
	u32 i, nb_samples;

	p = (GF_TimeToSampleBox *)a;
	DumpBox(a, "TimeToSampleBox", trace);
	gf_full_box_dump(a, trace);
	fprintf(trace, "EntryCount=\"%d\">\n", p->nb_entries);

	nb_samples = 0;
	for (i=0; i<p->nb_entries; i++) {
		fprintf(trace, "<TimeToSampleEntry SampleDelta=\"%d\" SampleCount=\"%d\"/>\n", p->entries[i].sampleDelta, p->entries[i].sampleCount);
		nb_samples += p->entries[i].sampleCount;
	}
	if (p->size)
		fprintf(trace, "<!-- counted %d samples in STTS entries -->\n", nb_samples);
	else
		fprintf(trace, "<TimeToSampleEntry SampleDelta=\"\" SampleCount=\"\"/>\n");

	gf_box_dump_done("TimeToSampleBox", a, trace);
	return GF_OK;
}

GF_Err ctts_dump(GF_Box *a, FILE * trace)
{
	GF_CompositionOffsetBox *p;
	u32 i, nb_samples;
	p = (GF_CompositionOffsetBox *)a;
	DumpBox(a, "CompositionOffsetBox", trace);
	gf_full_box_dump(a, trace);
	fprintf(trace, "EntryCount=\"%d\">\n", p->nb_entries);

	nb_samples = 0;
	for (i=0; i<p->nb_entries; i++) {
		fprintf(trace, "<CompositionOffsetEntry CompositionOffset=\"%d\" SampleCount=\"%d\"/>\n", p->entries[i].decodingOffset, p->entries[i].sampleCount);
		nb_samples += p->entries[i].sampleCount;
	}
	if (p->size)
		fprintf(trace, "<!-- counted %d samples in CTTS entries -->\n", nb_samples);
	else
		fprintf(trace, "<CompositionOffsetEntry CompositionOffset=\"\" SampleCount=\"\"/>\n");

	gf_box_dump_done("CompositionOffsetBox", a, trace);
	return GF_OK;
}

GF_Err cslg_dump(GF_Box *a, FILE * trace)
{
	GF_CompositionToDecodeBox *p;

	p = (GF_CompositionToDecodeBox *)a;
	DumpBox(a, "CompositionToDecodeBox", trace);
	gf_full_box_dump(a, trace);
	fprintf(trace, "compositionToDTSShift=\"%d\" leastDecodeToDisplayDelta=\"%d\" compositionStartTime=\"%d\" compositionEndTime=\"%d\">\n", p->leastDecodeToDisplayDelta, p->greatestDecodeToDisplayDelta, p->compositionStartTime, p->compositionEndTime);
	gf_box_dump_done("CompositionToDecodeBox", a, trace);
	return GF_OK;
}

GF_Err stsh_dump(GF_Box *a, FILE * trace)
{
	GF_ShadowSyncBox *p;
	u32 i;
	GF_StshEntry *t;

	p = (GF_ShadowSyncBox *)a;
	DumpBox(a, "SyncShadowBox", trace);
	gf_full_box_dump(a, trace);
	fprintf(trace, "EntryCount=\"%d\">\n", gf_list_count(p->entries));
	i=0;
	while ((t = (GF_StshEntry *)gf_list_enum(p->entries, &i))) {
		fprintf(trace, "<SyncShadowEntry ShadowedSample=\"%d\" SyncSample=\"%d\"/>\n", t->shadowedSampleNumber, t->syncSampleNumber);
	}
	if (!p->size) {
		fprintf(trace, "<SyncShadowEntry ShadowedSample=\"\" SyncSample=\"\"/>\n");
	}
	gf_box_dump_done("SyncShadowBox", a, trace);
	return GF_OK;
}

GF_Err elst_dump(GF_Box *a, FILE * trace)
{
	GF_EditListBox *p;
	u32 i;
	GF_EdtsEntry *t;

	p = (GF_EditListBox *)a;
	DumpBox(a, "EditListBox", trace);
	gf_full_box_dump(a, trace);
	fprintf(trace, "EntryCount=\"%d\">\n", gf_list_count(p->entryList));

	i=0;
	while ((t = (GF_EdtsEntry *)gf_list_enum(p->entryList, &i))) {
		fprintf(trace, "<EditListEntry Duration=\""LLD"\" MediaTime=\""LLD"\" MediaRate=\"%u\"/>\n", LLD_CAST t->segmentDuration, LLD_CAST t->mediaTime, t->mediaRate);
	}
	if (!p->size) {
		fprintf(trace, "<EditListEntry Duration=\"\" MediaTime=\"\" MediaRate=\"\"/>\n");
	}
	gf_box_dump_done("EditListBox", a, trace);
	return GF_OK;
}

GF_Err stsc_dump(GF_Box *a, FILE * trace)
{
	GF_SampleToChunkBox *p;
	u32 i, nb_samples;

	p = (GF_SampleToChunkBox *)a;
	DumpBox(a, "SampleToChunkBox", trace);
	gf_full_box_dump(a, trace);
	fprintf(trace, "EntryCount=\"%d\">\n", p->nb_entries);

	nb_samples = 0;
	for (i=0; i<p->nb_entries; i++) {
		fprintf(trace, "<SampleToChunkEntry FirstChunk=\"%d\" SamplesPerChunk=\"%d\" SampleDescriptionIndex=\"%d\"/>\n", p->entries[i].firstChunk, p->entries[i].samplesPerChunk, p->entries[i].sampleDescriptionIndex);
		if (i+1<p->nb_entries) {
			nb_samples += (p->entries[i+1].firstChunk - p->entries[i].firstChunk) * p->entries[i].samplesPerChunk;
		} else {
			nb_samples += p->entries[i].samplesPerChunk;
		}
	}
	if (p->size)
		fprintf(trace, "<!-- counted %d samples in STSC entries (could be less than sample count) -->\n", nb_samples);
	else
		fprintf(trace, "<SampleToChunkEntry FirstChunk=\"\" SamplesPerChunk=\"\" SampleDescriptionIndex=\"\"/>\n");

	gf_box_dump_done("SampleToChunkBox", a, trace);
	return GF_OK;
}

GF_Err stsz_dump(GF_Box *a, FILE * trace)
{
	GF_SampleSizeBox *p;
	u32 i;
	p = (GF_SampleSizeBox *)a;

	if (a->type == GF_ISOM_BOX_TYPE_STSZ) {
		DumpBox(a, "SampleSizeBox", trace);
	}
	else {
		DumpBox(a, "CompactSampleSizeBox", trace);
	}
	gf_full_box_dump(a, trace);

	fprintf(trace, "SampleCount=\"%d\"",  p->sampleCount);
	if (a->type == GF_ISOM_BOX_TYPE_STSZ) {
		if (p->sampleSize) {
			fprintf(trace, " ConstantSampleSize=\"%d\"", p->sampleSize);
		}
	} else {
		fprintf(trace, " SampleSizeBits=\"%d\"", p->sampleSize);
	}
	fprintf(trace, ">\n");

	if ((a->type != GF_ISOM_BOX_TYPE_STSZ) || !p->sampleSize) {
		if (!p->sizes && p->size) {
			fprintf(trace, "<!--WARNING: No Sample Size indications-->\n");
		} else {
			for (i=0; i<p->sampleCount; i++) {
				fprintf(trace, "<SampleSizeEntry Size=\"%d\"/>\n", p->sizes[i]);
			}
		}
	}
	if (!p->size) {
		fprintf(trace, "<SampleSizeEntry Size=\"\"/>\n");
	}
	gf_box_dump_done((a->type == GF_ISOM_BOX_TYPE_STSZ) ? "SampleSizeBox" : "CompactSampleSizeBox", a, trace);
	return GF_OK;
}

GF_Err stco_dump(GF_Box *a, FILE * trace)
{
	GF_ChunkOffsetBox *p;
	u32 i;

	p = (GF_ChunkOffsetBox *)a;
	DumpBox(a, "ChunkOffsetBox", trace);
	gf_full_box_dump(a, trace);
	fprintf(trace, "EntryCount=\"%d\">\n", p->nb_entries);

	if (!p->offsets && p->size) {
		fprintf(trace, "<!--Warning: No Chunk Offsets indications-->\n");
	} else {
		for (i=0; i<p->nb_entries; i++) {
			fprintf(trace, "<ChunkEntry offset=\"%u\"/>\n", p->offsets[i]);
		}
	}
	if (!p->size) {
		fprintf(trace, "<ChunkEntry offset=\"\"/>\n");
	}
	gf_box_dump_done("ChunkOffsetBox", a, trace);
	return GF_OK;
}

GF_Err stss_dump(GF_Box *a, FILE * trace)
{
	GF_SyncSampleBox *p;
	u32 i;

	p = (GF_SyncSampleBox *)a;
	DumpBox(a, "SyncSampleBox", trace);
	gf_full_box_dump(a, trace);
	fprintf(trace, "EntryCount=\"%d\">\n", p->nb_entries);

	if (!p->sampleNumbers && p->size) {
		fprintf(trace, "<!--Warning: No Key Frames indications-->\n");
	} else {
		for (i=0; i<p->nb_entries; i++) {
			fprintf(trace, "<SyncSampleEntry sampleNumber=\"%u\"/>\n", p->sampleNumbers[i]);
		}
	}
	if (!p->size) {
			fprintf(trace, "<SyncSampleEntry sampleNumber=\"\"/>\n");
	}
	gf_box_dump_done("SyncSampleBox", a, trace);
	return GF_OK;
}

GF_Err stdp_dump(GF_Box *a, FILE * trace)
{
	GF_DegradationPriorityBox *p;
	u32 i;

	p = (GF_DegradationPriorityBox *)a;
	DumpBox(a, "DegradationPriorityBox", trace);
	gf_full_box_dump(a, trace);
	fprintf(trace, "EntryCount=\"%d\">\n", p->nb_entries);

	if (!p->priorities && p->size) {
		fprintf(trace, "<!--Warning: No Degradation Priority indications-->\n");
	} else {
		for (i=0; i<p->nb_entries; i++) {
			fprintf(trace, "<DegradationPriorityEntry DegradationPriority=\"%d\"/>\n", p->priorities[i]);
		}
	}
	if (!p->size) {
		fprintf(trace, "<DegradationPriorityEntry DegradationPriority=\"\"/>\n");
	}
	gf_box_dump_done("DegradationPriorityBox", a, trace);
	return GF_OK;
}

GF_Err sdtp_dump(GF_Box *a, FILE * trace)
{
	GF_SampleDependencyTypeBox *p;
	u32 i;

	p = (GF_SampleDependencyTypeBox*)a;
	DumpBox(a, "SampleDependencyTypeBox", trace);
	gf_full_box_dump(a, trace);
	fprintf(trace, "SampleCount=\"%d\">\n", p->sampleCount);

	if (!p->sample_info && p->size) {
		fprintf(trace, "<!--Warning: No sample dependencies indications-->\n");
	} else {
		for (i=0; i<p->sampleCount; i++) {
			u8 flag = p->sample_info[i];
			fprintf(trace, "<SampleDependencyEntry ");
			switch ( (flag >> 4) & 3) {
			case 0:
				fprintf(trace, "dependsOnOther=\"unknown\" ");
				break;
			case 1:
				fprintf(trace, "dependsOnOther=\"yes\" ");
				break;
			case 2:
				fprintf(trace, "dependsOnOther=\"no\" ");
				break;
			case 3:
				fprintf(trace, "dependsOnOther=\"RESERVED\" ");
				break;
			}
			switch ( (flag >> 2) & 3) {
			case 0:
				fprintf(trace, "dependedOn=\"unknown\" ");
				break;
			case 1:
				fprintf(trace, "dependedOn=\"yes\" ");
				break;
			case 2:
				fprintf(trace, "dependedOn=\"no\" ");
				break;
			case 3:
				fprintf(trace, "dependedOn=\"RESERVED\" ");
				break;
			}
			switch ( flag & 3) {
			case 0:
				fprintf(trace, "hasRedundancy=\"unknown\" ");
				break;
			case 1:
				fprintf(trace, "hasRedundancy=\"yes\" ");
				break;
			case 2:
				fprintf(trace, "hasRedundancy=\"no\" ");
				break;
			case 3:
				fprintf(trace, "hasRedundancy=\"RESERVED\" ");
				break;
			}
			fprintf(trace, " />\n");
		}
	}
	if (!p->size) {
		fprintf(trace, "<SampleDependencyEntry dependsOnOther=\"unknown|yes|no|RESERVED\" dependedOn=\"unknown|yes|no|RESERVED\" hasRedundancy=\"unknown|yes|no|RESERVED\"/>\n");
	}
	gf_box_dump_done("SampleDependencyTypeBox", a, trace);
	return GF_OK;
}

GF_Err co64_dump(GF_Box *a, FILE * trace)
{
	GF_ChunkLargeOffsetBox *p;
	u32 i;

	p = (GF_ChunkLargeOffsetBox *)a;
	DumpBox(a, "ChunkLargeOffsetBox", trace);
	gf_full_box_dump(a, trace);
	fprintf(trace, "EntryCount=\"%d\">\n", p->nb_entries);

	if (!p->offsets && p->size) {
		fprintf(trace, "<!-- Warning: No Chunk Offsets indications/>\n");
	} else {
		for (i=0; i<p->nb_entries; i++)
			fprintf(trace, "<ChunkOffsetEntry offset=\""LLU"\"/>\n", LLU_CAST p->offsets[i]);
	}
	if (!p->size) {
		fprintf(trace, "<ChunkOffsetEntry offset=\"\"/>\n");
	}
	gf_box_dump_done("ChunkLargeOffsetBox", a, trace);
	return GF_OK;
}

GF_Err esds_dump(GF_Box *a, FILE * trace)
{
	GF_ESDBox *p;

	p = (GF_ESDBox *)a;
	DumpBox(a, "MPEG4ESDescriptorBox", trace);
	gf_full_box_dump(a, trace);
	fprintf(trace, ">\n");

	if (p->desc) {
#ifndef GPAC_DISABLE_OD_DUMP
		gf_odf_dump_desc((GF_Descriptor *) p->desc, trace, 1, GF_TRUE);
#else
		fprintf(trace, "<!-- Object Descriptor Dumping disabled in this build of GPAC -->\n");
#endif
	} else if (p->size) {
		fprintf(trace, "<!--INVALID MP4 FILE: ESD not present in MPEG Sample Description or corrupted-->\n");
	}
	gf_box_dump_done("MPEG4ESDescriptorBox", a, trace);
	return GF_OK;
}

GF_Err minf_dump(GF_Box *a, FILE * trace)
{
	GF_MediaInformationBox *p;

	p = (GF_MediaInformationBox *)a;
	DumpBox(a, "MediaInformationBox", trace);
	fprintf(trace, ">\n");

	if (p->size)
		gf_box_dump_ex(p->InfoHeader, trace, GF_ISOM_BOX_TYPE_NMHD);
	if (p->size)
		gf_box_dump_ex(p->dataInformation, trace, GF_ISOM_BOX_TYPE_DINF);
	if (p->size)
		gf_box_dump_ex(p->sampleTable, trace, GF_ISOM_BOX_TYPE_STBL);

	gf_box_dump_done("MediaInformationBox", a, trace);
	return GF_OK;
}

GF_Err tkhd_dump(GF_Box *a, FILE * trace)
{
	GF_TrackHeaderBox *p;
	p = (GF_TrackHeaderBox *)a;
	DumpBox(a, "TrackHeaderBox", trace);
	gf_full_box_dump(a, trace);

	fprintf(trace, "CreationTime=\""LLD"\" ModificationTime=\""LLD"\" TrackID=\"%u\" Duration=\""LLD"\"",
	        LLD_CAST p->creationTime, LLD_CAST p->modificationTime, p->trackID, LLD_CAST p->duration);

	if (p->alternate_group) fprintf(trace, " AlternateGroupID=\"%d\"", p->alternate_group);
	if (p->volume) {
		fprintf(trace, " Volume=\"%.2f\"", (Float)p->volume / 256);
	} else if (p->width || p->height) {
		fprintf(trace, " Width=\"%.2f\" Height=\"%.2f\"", (Float)p->width / 65536, (Float)p->height / 65536);
		if (p->layer) fprintf(trace, " Layer=\"%d\"", p->layer);
	}
	fprintf(trace, ">\n");
	if (p->width || p->height) {
		fprintf(trace, "<Matrix m11=\"0x%.8x\" m12=\"0x%.8x\" m13=\"0x%.8x\" ", p->matrix[0], p->matrix[1], p->matrix[2]);
		fprintf(trace, "m21=\"0x%.8x\" m22=\"0x%.8x\" m23=\"0x%.8x\" ", p->matrix[3], p->matrix[4], p->matrix[5]);
		fprintf(trace, "m31=\"0x%.8x\" m32=\"0x%.8x\" m33=\"0x%.8x\"/>\n", p->matrix[6], p->matrix[7], p->matrix[8]);
	}

	gf_box_dump_done("TrackHeaderBox", a, trace);
	return GF_OK;
}

GF_Err tref_dump(GF_Box *a, FILE * trace)
{
//	GF_TrackReferenceBox *p = (GF_TrackReferenceBox *)a;
	DumpBox(a, "TrackReferenceBox", trace);
	fprintf(trace, ">\n");
	gf_box_dump_done("TrackReferenceBox", a, trace);
	return GF_OK;
}

GF_Err mdia_dump(GF_Box *a, FILE * trace)
{
	GF_MediaBox *p = (GF_MediaBox *)a;
	DumpBox(a, "MediaBox", trace);
	fprintf(trace, ">\n");
	if (p->size)
		gf_box_dump_ex(p->mediaHeader, trace, GF_ISOM_BOX_TYPE_MDHD);
	if (p->size)
		gf_box_dump_ex(p->handler, trace,GF_ISOM_BOX_TYPE_HDLR);
	if (p->size)
		gf_box_dump_ex(p->information, trace, GF_ISOM_BOX_TYPE_MINF);
	gf_box_dump_done("MediaBox", a, trace);
	return GF_OK;
}

GF_Err mfra_dump(GF_Box *a, FILE * trace)
{
	GF_MovieFragmentRandomAccessBox *p = (GF_MovieFragmentRandomAccessBox *)a;
	u32 i, count;
	GF_TrackFragmentRandomAccessBox *tfra;

	DumpBox(a, "MovieFragmentRandomAccessBox", trace);
	fprintf(trace, ">\n");
	count = gf_list_count(p->tfra_list);
	for (i=0; i<count; i++) {
		tfra = (GF_TrackFragmentRandomAccessBox *)gf_list_get(p->tfra_list, i);
		gf_box_dump_ex(tfra, trace, GF_ISOM_BOX_TYPE_TFRA);
	}
	gf_box_dump_done("MovieFragmentRandomAccessBox", a, trace);
	return GF_OK;
}

GF_Err tfra_dump(GF_Box *a, FILE * trace)
{
	u32 i;
	GF_TrackFragmentRandomAccessBox *p = (GF_TrackFragmentRandomAccessBox *)a;
	DumpBox(a, "TrackFragmentRandomAccessBox", trace);
	fprintf(trace, "TrackId=\"%u\" number_of_entries=\"%u\">\n", p->track_id, p->nb_entries);
	for (i=0; i<p->nb_entries; i++) {
		fprintf(trace, "<RandomAccessEntry time=\""LLU"\" moof_offset=\""LLU"\" traf=\"%u\" trun=\"%u\" sample=\"%u\"/>\n",
			p->entries[i].time, p->entries[i].moof_offset,
			p->entries[i].traf_number, p->entries[i].trun_number, p->entries[i].sample_number);
	}
	if (!p->size) {
		fprintf(trace, "<RandomAccessEntry time=\"\" moof_offset=\"\" traf=\"\" trun=\"\" sample=\"\"/>\n");
	}
	gf_box_dump_done("TrackFragmentRandomAccessBox", a, trace);
	return GF_OK;
}

GF_Err elng_dump(GF_Box *a, FILE * trace)
{
	GF_ExtendedLanguageBox *p = (GF_ExtendedLanguageBox *)a;
	DumpBox(a, "ExtendedLanguageBox", trace);
	fprintf(trace, "LanguageCode=\"%s\">\n", p->extended_language);
	gf_box_dump_done("ExtendedLanguageBox", a, trace);
	return GF_OK;
}

GF_Err unkn_dump(GF_Box *a, FILE * trace)
{
	GF_UnknownBox *u = (GF_UnknownBox *)a;
	u->type = u->original_4cc;
	DumpBox(a, "UnknownBox", trace);
	u->type = GF_ISOM_BOX_TYPE_UNKNOWN;
	fprintf(trace, ">\n");
	fprintf(trace, "</UnknownBox>\n");
	return GF_OK;
}

GF_Err uuid_dump(GF_Box *a, FILE * trace)
{
	DumpBox(a, "UnknownUUIDBox", trace);
	fprintf(trace, ">\n");
	fprintf(trace, "</UnknownUUIDBox>\n");
	return GF_OK;
}

GF_Err void_dump(GF_Box *a, FILE * trace)
{
	DumpBox(a, "VoidBox", trace);
	fprintf(trace, ">\n");
	fprintf(trace, "</VoidBox>\n");
	return GF_OK;
}

GF_Err ftyp_dump(GF_Box *a, FILE * trace)
{
	GF_FileTypeBox *p;
	u32 i;

	p = (GF_FileTypeBox *)a;
	DumpBox(a, (a->type == GF_ISOM_BOX_TYPE_FTYP ? "FileTypeBox" : "SegmentTypeBox"), trace);
	fprintf(trace, "MajorBrand=\"%s\" MinorVersion=\"%d\">\n", gf_4cc_to_str(p->majorBrand), p->minorVersion);

	for (i=0; i<p->altCount; i++) {
		fprintf(trace, "<BrandEntry AlternateBrand=\"%s\"/>\n", gf_4cc_to_str(p->altBrand[i]));
	}
	if (!p->type) {
		fprintf(trace, "<BrandEntry AlternateBrand=\"4CC\"/>\n");
	}
	gf_box_dump_done((a->type == GF_ISOM_BOX_TYPE_FTYP ? "FileTypeBox" : "SegmentTypeBox"), a, trace);
	return GF_OK;
}

GF_Err padb_dump(GF_Box *a, FILE * trace)
{
	GF_PaddingBitsBox *p;
	u32 i;

	p = (GF_PaddingBitsBox *)a;
	DumpBox(a, "PaddingBitsBox", trace);
	fprintf(trace, "EntryCount=\"%d\">\n", p->SampleCount);
	for (i=0; i<p->SampleCount; i+=1) {
		fprintf(trace, "<PaddingBitsEntry PaddingBits=\"%d\"/>\n", p->padbits[i]);
	}
	if (!p->size) {
		fprintf(trace, "<PaddingBitsEntry PaddingBits=\"\"/>\n");
	}
	gf_box_dump_done("PaddingBitsBox", a, trace);
	return GF_OK;
}

GF_Err stsf_dump(GF_Box *a, FILE * trace)
{
	GF_SampleFragmentBox *p;
	GF_StsfEntry *ent;
	u32 i, j, count;


	p = (GF_SampleFragmentBox *)a;
	count = gf_list_count(p->entryList);
	DumpBox(a, "SampleFragmentBox", trace);
	fprintf(trace, "EntryCount=\"%d\">\n", count);

	for (i=0; i<count; i++) {
		ent = (GF_StsfEntry *)gf_list_get(p->entryList, i);
		fprintf(trace, "<SampleFragmentEntry SampleNumber=\"%d\" FragmentCount=\"%d\">\n", ent->SampleNumber, ent->fragmentCount);
		for (j=0; j<ent->fragmentCount; j++) fprintf(trace, "<FragmentSizeEntry size=\"%d\"/>\n", ent->fragmentSizes[j]);
		fprintf(trace, "</SampleFragmentEntry>\n");
	}
	if (!p->size) {
		fprintf(trace, "<SampleFragmentEntry SampleNumber=\"\" FragmentCount=\"\">\n");
		fprintf(trace, "<FragmentSizeEntry size=\"\"/>\n");
		fprintf(trace, "</SampleFragmentEntry>\n");
	}
	gf_box_dump_done("SampleFragmentBox", a, trace);
	return GF_OK;
}


GF_Err gppa_dump(GF_Box *a, FILE * trace)
{
	char *szName;
	GF_3GPPAudioSampleEntryBox *p = (GF_3GPPAudioSampleEntryBox *)a;
	switch (p->type) {
	case GF_ISOM_SUBTYPE_3GP_AMR:
		szName = "AMRSampleDescriptionBox";
		break;
	case GF_ISOM_SUBTYPE_3GP_AMR_WB:
		szName = "AMR_WB_SampleDescriptionBox";
		break;
	case GF_ISOM_SUBTYPE_3GP_EVRC:
		szName = "EVRCSampleDescriptionBox";
		break;
	case GF_ISOM_SUBTYPE_3GP_QCELP:
		szName = "QCELPSampleDescriptionBox";
		break;
	case GF_ISOM_SUBTYPE_3GP_SMV:
		szName = "SMVSampleDescriptionBox";
		break;
	default:
		szName = "3GPAudioSampleDescriptionBox";
		break;
	}
	DumpBox(a, szName, trace);
	base_audio_entry_dump((GF_AudioSampleEntryBox *)p, trace);
	fprintf(trace, ">\n");

	if (p->info) {
		gf_box_dump(p->info, trace);
	} else if (p->size) {
		fprintf(trace, "<!-- INVALID 3GPP FILE: Config not present in Sample Description-->\n");
	}
	gf_box_dump_done(szName, a, trace);
	return GF_OK;
}

GF_Err gppv_dump(GF_Box *a, FILE * trace)
{
	char *szName;
	GF_3GPPVisualSampleEntryBox *p = (GF_3GPPVisualSampleEntryBox *)a;

	switch (p->type) {
	case GF_ISOM_SUBTYPE_3GP_H263:
		szName = "H263SampleDescriptionBox";
		break;
	default:
		szName = "3GPVisualSampleDescriptionBox";
		break;
	}
	DumpBox(a, szName, trace);
	base_visual_entry_dump((GF_VisualSampleEntryBox *)p, trace);
	fprintf(trace, ">\n");
	if (p->info) {
		gf_box_dump(p->info, trace);
	} else if (p->size) {
		fprintf(trace, "<!-- INVALID 3GPP FILE: Config not present in Sample Description-->\n");
	}
	gf_box_dump_done(szName, a, trace);
	return GF_OK;
}

GF_Err gppc_dump(GF_Box *a, FILE * trace)
{
	GF_3GPPConfigBox *p = (GF_3GPPConfigBox *)a;
	const char *name = gf_4cc_to_str(p->cfg.vendor);
	switch (p->cfg.type) {
	case GF_ISOM_SUBTYPE_3GP_AMR:
	case GF_ISOM_SUBTYPE_3GP_AMR_WB:
		DumpBox(a, "AMRConfigurationBox", trace);
		fprintf(trace, "Vendor=\"%s\" Version=\"%d\"", name, p->cfg.decoder_version);
		fprintf(trace, " FramesPerSample=\"%d\" SupportedModes=\"%x\" ModeRotating=\"%d\"", p->cfg.frames_per_sample, p->cfg.AMR_mode_set, p->cfg.AMR_mode_change_period);
		fprintf(trace, ">\n");
		gf_box_dump_done("AMRConfigurationBox", a, trace);
		break;
	case GF_ISOM_SUBTYPE_3GP_EVRC:
		DumpBox(a, "EVRCConfigurationBox", trace);
		fprintf(trace, "Vendor=\"%s\" Version=\"%d\" FramesPerSample=\"%d\" >\n", name, p->cfg.decoder_version, p->cfg.frames_per_sample);
		gf_box_dump_done("EVRCConfigurationBox", a, trace);
		break;
	case GF_ISOM_SUBTYPE_3GP_QCELP:
		DumpBox(a, "QCELPConfigurationBox", trace);
		fprintf(trace, "Vendor=\"%s\" Version=\"%d\" FramesPerSample=\"%d\" >\n", name, p->cfg.decoder_version, p->cfg.frames_per_sample);
		gf_box_dump_done("QCELPConfigurationBox", a, trace);
		break;
	case GF_ISOM_SUBTYPE_3GP_SMV:
		DumpBox(a, "SMVConfigurationBox", trace);
		fprintf(trace, "Vendor=\"%s\" Version=\"%d\" FramesPerSample=\"%d\" >\n", name, p->cfg.decoder_version, p->cfg.frames_per_sample);
		gf_box_dump_done("SMVConfigurationBox", a, trace);
		break;
	case GF_ISOM_SUBTYPE_3GP_H263:
		DumpBox(a, "H263ConfigurationBox", trace);
		fprintf(trace, "Vendor=\"%s\" Version=\"%d\"", name, p->cfg.decoder_version);
		fprintf(trace, " Profile=\"%d\" Level=\"%d\"", p->cfg.H263_profile, p->cfg.H263_level);
		fprintf(trace, ">\n");
		gf_box_dump_done("H263ConfigurationBox", a, trace);
		break;
	default:
		break;
	}
	return GF_OK;
}


GF_Err avcc_dump(GF_Box *a, FILE * trace)
{
	u32 i, count;
	GF_AVCConfigurationBox *p = (GF_AVCConfigurationBox *) a;
	const char *name = (p->type==GF_ISOM_BOX_TYPE_SVCC) ? "SVC" : "AVC";
	char boxname[256];
	sprintf(boxname, "%sConfigurationBox", name);
	DumpBox(a, boxname, trace);
	fprintf(trace, ">\n");

	fprintf(trace, "<%sDecoderConfigurationRecord", name);

	if (! p->config) {
		if (p->size) {
			fprintf(trace, ">\n");
			fprintf(trace, "<!-- INVALID AVC ENTRY : no AVC/SVC config record -->\n");
		} else {

			fprintf(trace, " configurationVersion=\"\" AVCProfileIndication=\"\" profile_compatibility=\"\" AVCLevelIndication=\"\" nal_unit_size=\"\" complete_representation=\"\"");

			fprintf(trace, " chroma_format=\"\" luma_bit_depth=\"\" chroma_bit_depth=\"\"");
			fprintf(trace, ">\n");

			fprintf(trace, "<SequenceParameterSet size=\"\" content=\"\"/>\n");
			fprintf(trace, "<PictureParameterSet size=\"\" content=\"\"/>\n");
			fprintf(trace, "<SequenceParameterSetExtensions size=\"\" content=\"\"/>\n");
		}
		fprintf(trace, "</%sDecoderConfigurationRecord>\n", name);
		gf_box_dump_done(boxname, a, trace);
		return GF_OK;
	}

	fprintf(trace, " configurationVersion=\"%d\" AVCProfileIndication=\"%d\" profile_compatibility=\"%d\" AVCLevelIndication=\"%d\" nal_unit_size=\"%d\"", p->config->configurationVersion, p->config->AVCProfileIndication, p->config->profile_compatibility, p->config->AVCLevelIndication, p->config->nal_unit_size);


	if (p->type==GF_ISOM_BOX_TYPE_SVCC)
		fprintf(trace, " complete_representation=\"%d\"", p->config->complete_representation);

	if (p->type==GF_ISOM_BOX_TYPE_AVCC) {
		if (gf_avc_is_rext_profile(p->config->AVCProfileIndication)) {
			fprintf(trace, " chroma_format=\"%s\" luma_bit_depth=\"%d\" chroma_bit_depth=\"%d\"", gf_avc_hevc_get_chroma_format_name(p->config->chroma_format), p->config->luma_bit_depth, p->config->chroma_bit_depth);
		}
	}

	fprintf(trace, ">\n");

	count = gf_list_count(p->config->sequenceParameterSets);
	for (i=0; i<count; i++) {
		GF_AVCConfigSlot *c = (GF_AVCConfigSlot *)gf_list_get(p->config->sequenceParameterSets, i);
		fprintf(trace, "<SequenceParameterSet size=\"%d\" content=\"", c->size);
		DumpData(trace, c->data, c->size);
		fprintf(trace, "\"/>\n");
	}
	count = gf_list_count(p->config->pictureParameterSets);
	for (i=0; i<count; i++) {
		GF_AVCConfigSlot *c = (GF_AVCConfigSlot *)gf_list_get(p->config->pictureParameterSets, i);
		fprintf(trace, "<PictureParameterSet size=\"%d\" content=\"", c->size);
		DumpData(trace, c->data, c->size);
		fprintf(trace, "\"/>\n");
	}

	if (p->config->sequenceParameterSetExtensions) {
		count = gf_list_count(p->config->sequenceParameterSetExtensions);
		for (i=0; i<count; i++) {
			GF_AVCConfigSlot *c = (GF_AVCConfigSlot *)gf_list_get(p->config->sequenceParameterSetExtensions, i);
			fprintf(trace, "<SequenceParameterSetExtensions size=\"%d\" content=\"", c->size);
			DumpData(trace, c->data, c->size);
			fprintf(trace, "\"/>\n");
		}
	}

	fprintf(trace, "</%sDecoderConfigurationRecord>\n", name);

	gf_box_dump_done(boxname, a, trace);
	return GF_OK;
}

GF_Err hvcc_dump(GF_Box *a, FILE * trace)
{
	u32 i, count;
	const char *name = (a->type==GF_ISOM_BOX_TYPE_HVCC) ? "HEVC" : "L-HEVC";
	char boxname[256];
	GF_HEVCConfigurationBox *p = (GF_HEVCConfigurationBox *) a;

	sprintf(boxname, "%sConfigurationBox", name);
	DumpBox(a, boxname, trace);
	fprintf(trace, ">\n");

	if (! p->config) {
		if (p->size) {
			fprintf(trace, "<!-- INVALID HEVC ENTRY: no HEVC/SHVC config record -->\n");
		} else {
			fprintf(trace, "<%sDecoderConfigurationRecord nal_unit_size=\"\" configurationVersion=\"\" ", name);
			if (a->type==GF_ISOM_BOX_TYPE_HVCC) {
				fprintf(trace, "profile_space=\"\" tier_flag=\"\" profile_idc=\"\" general_profile_compatibility_flags=\"\" progressive_source_flag=\"\" interlaced_source_flag=\"\" non_packed_constraint_flag=\"\" frame_only_constraint_flag=\"\" constraint_indicator_flags=\"\" level_idc=\"\" ");
			}
			fprintf(trace, "min_spatial_segmentation_idc=\"\" parallelismType=\"\" ");

			if (a->type==GF_ISOM_BOX_TYPE_HVCC)
				fprintf(trace, "chroma_format=\"\" luma_bit_depth=\"\" chroma_bit_depth=\"\" avgFrameRate=\"\" constantFrameRate=\"\" numTemporalLayers=\"\" temporalIdNested=\"\"");

			fprintf(trace, ">\n");
			fprintf(trace, "<ParameterSetArray nalu_type=\"\" complete_set=\"\">\n");
			fprintf(trace, "<ParameterSet size=\"\" content=\"\"/>\n");
			fprintf(trace, "</ParameterSetArray>\n");
			fprintf(trace, "</%sDecoderConfigurationRecord>\n", name);
		}
		fprintf(trace, "</%sConfigurationBox>\n", name);
		return GF_OK;
	}

	fprintf(trace, "<%sDecoderConfigurationRecord nal_unit_size=\"%d\" ", name, p->config->nal_unit_size);
	fprintf(trace, "configurationVersion=\"%d\" ", p->config->configurationVersion);
	if (a->type==GF_ISOM_BOX_TYPE_HVCC) {
		fprintf(trace, "profile_space=\"%d\" ", p->config->profile_space);
		fprintf(trace, "tier_flag=\"%d\" ", p->config->tier_flag);
		fprintf(trace, "profile_idc=\"%d\" ", p->config->profile_idc);
		fprintf(trace, "general_profile_compatibility_flags=\"%d\" ", p->config->general_profile_compatibility_flags);
		fprintf(trace, "progressive_source_flag=\"%d\" ", p->config->progressive_source_flag);
		fprintf(trace, "interlaced_source_flag=\"%d\" ", p->config->interlaced_source_flag);
		fprintf(trace, "non_packed_constraint_flag=\"%d\" ", p->config->non_packed_constraint_flag);
		fprintf(trace, "frame_only_constraint_flag=\"%d\" ", p->config->frame_only_constraint_flag);
		fprintf(trace, "constraint_indicator_flags=\""LLD"\" ", p->config->constraint_indicator_flags);
		fprintf(trace, "level_idc=\"%d\" ", p->config->level_idc);
	}
	fprintf(trace, "min_spatial_segmentation_idc=\"%d\" ", p->config->min_spatial_segmentation_idc);
	fprintf(trace, "parallelismType=\"%d\" ", p->config->parallelismType);

	if (a->type==GF_ISOM_BOX_TYPE_HVCC)
		fprintf(trace, "chroma_format=\"%s\" luma_bit_depth=\"%d\" chroma_bit_depth=\"%d\" avgFrameRate=\"%d\" constantFrameRate=\"%d\" numTemporalLayers=\"%d\" temporalIdNested=\"%d\"",
	        gf_avc_hevc_get_chroma_format_name(p->config->chromaFormat), p->config->luma_bit_depth, p->config->chroma_bit_depth, p->config->avgFrameRate, p->config->constantFrameRate, p->config->numTemporalLayers, p->config->temporalIdNested);

	fprintf(trace, ">\n");

	count = gf_list_count(p->config->param_array);
	for (i=0; i<count; i++) {
		u32 nalucount, j;
		GF_HEVCParamArray *ar = (GF_HEVCParamArray*)gf_list_get(p->config->param_array, i);
		fprintf(trace, "<ParameterSetArray nalu_type=\"%d\" complete_set=\"%d\">\n", ar->type, ar->array_completeness);
		nalucount = gf_list_count(ar->nalus);
		for (j=0; j<nalucount; j++) {
			GF_AVCConfigSlot *c = (GF_AVCConfigSlot *)gf_list_get(ar->nalus, j);
			fprintf(trace, "<ParameterSet size=\"%d\" content=\"", c->size);
			DumpData(trace, c->data, c->size);
			fprintf(trace, "\"/>\n");
		}
		fprintf(trace, "</ParameterSetArray>\n");
	}

	fprintf(trace, "</%sDecoderConfigurationRecord>\n", name);

	gf_box_dump_done(boxname, a, trace);
	return GF_OK;
}

GF_Err m4ds_dump(GF_Box *a, FILE * trace)
{
	u32 i;
	GF_Descriptor *desc;
	GF_MPEG4ExtensionDescriptorsBox *p = (GF_MPEG4ExtensionDescriptorsBox *) a;
	DumpBox(a, "MPEG4ExtensionDescriptorsBox", trace);
	fprintf(trace, ">\n");

	i=0;
	while ((desc = (GF_Descriptor *)gf_list_enum(p->descriptors, &i))) {
#ifndef GPAC_DISABLE_OD_DUMP
		gf_odf_dump_desc(desc, trace, 1, GF_TRUE);
#else
		fprintf(trace, "<!-- Object Descriptor Dumping disabled in this build of GPAC -->\n");
#endif
	}
	gf_box_dump_done("MPEG4ExtensionDescriptorsBox", a, trace);
	return GF_OK;
}

GF_Err btrt_dump(GF_Box *a, FILE * trace)
{
	GF_BitRateBox *p = (GF_BitRateBox*)a;
	DumpBox(a, "BitRateBox", trace);
	fprintf(trace, "BufferSizeDB=\"%d\" avgBitRate=\"%d\" maxBitRate=\"%d\">\n", p->bufferSizeDB, p->avgBitrate, p->maxBitrate);
	gf_box_dump_done("BitRateBox", a, trace);
	return GF_OK;
}

GF_Err ftab_dump(GF_Box *a, FILE * trace)
{
	u32 i;
	GF_FontTableBox *p = (GF_FontTableBox *)a;
	DumpBox(a, "FontTableBox", trace);
	fprintf(trace, ">\n");
	for (i=0; i<p->entry_count; i++) {
		fprintf(trace, "<FontRecord ID=\"%d\" name=\"%s\"/>\n", p->fonts[i].fontID, p->fonts[i].fontName ? p->fonts[i].fontName : "NULL");
	}
	if (!p->size) {
		fprintf(trace, "<FontRecord ID=\"\" name=\"\"/>\n");
	}
	gf_box_dump_done("FontTableBox", a, trace);
	return GF_OK;
}

static void gpp_dump_rgba8(FILE * trace, char *name, u32 col)
{
	fprintf(trace, "%s=\"%x %x %x %x\"", name, (col>>16)&0xFF, (col>>8)&0xFF, (col)&0xFF, (col>>24)&0xFF);
}
static void gpp_dump_rgb16(FILE * trace, char *name, char col[6])
{
	fprintf(trace, "%s=\"%x %x %x\"", name, *((u16*)col), *((u16*)(col+1)), *((u16*)(col+2)));
}
static void gpp_dump_box(FILE * trace, GF_BoxRecord *rec)
{
	fprintf(trace, "<BoxRecord top=\"%d\" left=\"%d\" bottom=\"%d\" right=\"%d\"/>\n", rec->top, rec->left, rec->bottom, rec->right);
}
static void gpp_dump_style(FILE * trace, GF_StyleRecord *rec)
{
	fprintf(trace, "<StyleRecord startChar=\"%d\" endChar=\"%d\" fontID=\"%d\" styles=\"", rec->startCharOffset, rec->endCharOffset, rec->fontID);
	if (!rec->style_flags) {
		fprintf(trace, "Normal");
	} else {
		if (rec->style_flags & 1) fprintf(trace, "Bold ");
		if (rec->style_flags & 2) fprintf(trace, "Italic ");
		if (rec->style_flags & 4) fprintf(trace, "Underlined ");
	}
	fprintf(trace, "\" fontSize=\"%d\" ", rec->font_size);
	gpp_dump_rgba8(trace, "textColor", rec->text_color);
	fprintf(trace, "/>\n");
}

GF_Err tx3g_dump(GF_Box *a, FILE * trace)
{
	GF_Tx3gSampleEntryBox *p = (GF_Tx3gSampleEntryBox *)a;
	DumpBox(a, "Tx3gSampleEntryBox", trace);
	fprintf(trace, "dataReferenceIndex=\"%d\" displayFlags=\"%x\" horizontal-justification=\"%d\" vertical-justification=\"%d\" ",
	        p->dataReferenceIndex, p->displayFlags, p->horizontal_justification, p->vertical_justification);

	gpp_dump_rgba8(trace, "backgroundColor", p->back_color);
	fprintf(trace, ">\n");
	fprintf(trace, "<DefaultBox>\n");
	gpp_dump_box(trace, &p->default_box);
	gf_box_dump_done("DefaultBox", a, trace);
	fprintf(trace, "<DefaultStyle>\n");
	gpp_dump_style(trace, &p->default_style);
	fprintf(trace, "</DefaultStyle>\n");
	if (p->size) {
		gf_box_dump_ex(p->font_table, trace, GF_ISOM_BOX_TYPE_FTAB);
	}
	gf_box_dump_done("Tx3gSampleEntryBox", a, trace);
	return GF_OK;
}

GF_Err text_dump(GF_Box *a, FILE * trace)
{
	GF_TextSampleEntryBox *p = (GF_TextSampleEntryBox *)a;
	DumpBox(a, "TextSampleEntryBox", trace);
	fprintf(trace, "dataReferenceIndex=\"%d\" displayFlags=\"%x\" textJustification=\"%d\"  ",
	        p->dataReferenceIndex, p->displayFlags, p->textJustification);
	if (p->textName)
		fprintf(trace, "textName=\"%s\" ", p->textName);
	gpp_dump_rgb16(trace, "background-color", p->background_color);
	gpp_dump_rgb16(trace, " foreground-color", p->foreground_color);
	fprintf(trace, ">\n");

	fprintf(trace, "<DefaultBox>\n");
	gpp_dump_box(trace, &p->default_box);
	gf_box_dump_done("DefaultBox", a, trace);
	gf_box_dump_done("TextSampleEntryBox", a, trace);
	return GF_OK;
}

GF_Err styl_dump(GF_Box *a, FILE * trace)
{
	u32 i;
	GF_TextStyleBox*p = (GF_TextStyleBox*)a;
	DumpBox(a, "TextStyleBox", trace);
	fprintf(trace, ">\n");
	for (i=0; i<p->entry_count; i++) gpp_dump_style(trace, &p->styles[i]);
	if (!p->size) {
		fprintf(trace, "<StyleRecord startChar=\"\" endChar=\"\" fontID=\"\" styles=\"Normal|Bold|Italic|Underlined\" fontSize=\"\" textColor=\"\" />\n");
	}
	gf_box_dump_done("TextStyleBox", a, trace);
	return GF_OK;
}
GF_Err hlit_dump(GF_Box *a, FILE * trace)
{
	GF_TextHighlightBox*p = (GF_TextHighlightBox*)a;
	DumpBox(a, "TextHighlightBox", trace);
	fprintf(trace, "startcharoffset=\"%d\" endcharoffset=\"%d\">\n", p->startcharoffset, p->endcharoffset);
	gf_box_dump_done("TextHighlightBox", a, trace);
	return GF_OK;
}
GF_Err hclr_dump(GF_Box *a, FILE * trace)
{
	GF_TextHighlightColorBox*p = (GF_TextHighlightColorBox*)a;
	DumpBox(a, "TextHighlightColorBox", trace);
	gpp_dump_rgba8(trace, "highlight_color", p->hil_color);
	fprintf(trace, ">\n");
	gf_box_dump_done("TextHighlightColorBox", a, trace);
	return GF_OK;
}

GF_Err krok_dump(GF_Box *a, FILE * trace)
{
	u32 i;
	GF_TextKaraokeBox*p = (GF_TextKaraokeBox*)a;
	DumpBox(a, "TextKaraokeBox", trace);
	fprintf(trace, "highlight_starttime=\"%d\">\n", p->highlight_starttime);
	for (i=0; i<p->nb_entries; i++) {
		fprintf(trace, "<KaraokeRecord highlight_endtime=\"%d\" start_charoffset=\"%d\" end_charoffset=\"%d\"/>\n", p->records[i].highlight_endtime, p->records[i].start_charoffset, p->records[i].end_charoffset);
	}
	if (!p->size) {
		fprintf(trace, "<KaraokeRecord highlight_endtime=\"\" start_charoffset=\"\" end_charoffset=\"\"/>\n");
	}
	gf_box_dump_done("TextKaraokeBox", a, trace);
	return GF_OK;
}
GF_Err dlay_dump(GF_Box *a, FILE * trace)
{
	GF_TextScrollDelayBox*p = (GF_TextScrollDelayBox*)a;
	DumpBox(a, "TextScrollDelayBox", trace);
	fprintf(trace, "scroll_delay=\"%d\">\n", p->scroll_delay);
	gf_box_dump_done("TextScrollDelayBox", a, trace);
	return GF_OK;
}
GF_Err href_dump(GF_Box *a, FILE * trace)
{
	GF_TextHyperTextBox*p = (GF_TextHyperTextBox*)a;
	DumpBox(a, "TextHyperTextBox", trace);
	fprintf(trace, "startcharoffset=\"%d\" endcharoffset=\"%d\" URL=\"%s\" altString=\"%s\">\n", p->startcharoffset, p->endcharoffset, p->URL ? p->URL : "NULL", p->URL_hint ? p->URL_hint : "NULL");
	gf_box_dump_done("TextHyperTextBox", a, trace);
	return GF_OK;
}
GF_Err tbox_dump(GF_Box *a, FILE * trace)
{
	GF_TextBoxBox*p = (GF_TextBoxBox*)a;
	DumpBox(a, "TextBoxBox", trace);
	fprintf(trace, ">\n");
	gpp_dump_box(trace, &p->box);
	gf_box_dump_done("TextBoxBox", a, trace);
	return GF_OK;
}
GF_Err blnk_dump(GF_Box *a, FILE * trace)
{
	GF_TextBlinkBox*p = (GF_TextBlinkBox*)a;
	DumpBox(a, "TextBlinkBox", trace);
	fprintf(trace, "start_charoffset=\"%d\" end_charoffset=\"%d\">\n", p->startcharoffset, p->endcharoffset);
	gf_box_dump_done("TextBlinkBox", a, trace);
	return GF_OK;
}
GF_Err twrp_dump(GF_Box *a, FILE * trace)
{
	GF_TextWrapBox*p = (GF_TextWrapBox*)a;
	DumpBox(a, "TextWrapBox", trace);
	fprintf(trace, "wrap_flag=\"%s\">\n", p->wrap_flag ? ( (p->wrap_flag>1) ? "Reserved" : "Automatic" ) : "No Wrap");
	gf_box_dump_done("TextWrapBox", a, trace);
	return GF_OK;
}


GF_Err meta_dump(GF_Box *a, FILE * trace)
{
	GF_MetaBox *p;
	p = (GF_MetaBox *)a;
	DumpBox(a, "MetaBox", trace);
	gf_full_box_dump(a, trace);
	fprintf(trace, ">\n");

	if (p->handler) gf_box_dump(p->handler, trace);
	if (p->primary_resource) gf_box_dump(p->primary_resource, trace);
	if (p->file_locations) gf_box_dump(p->file_locations, trace);
	if (p->item_locations) gf_box_dump(p->item_locations, trace);
	if (p->protections) gf_box_dump(p->protections, trace);
	if (p->item_infos) gf_box_dump(p->item_infos, trace);
	if (p->IPMP_control) gf_box_dump(p->IPMP_control, trace);
	if (p->item_refs) gf_box_dump(p->item_refs, trace);
	if (p->item_props) gf_box_dump(p->item_props, trace);
	gf_box_dump_done("MetaBox", a, trace);
	return GF_OK;
}


GF_Err xml_dump(GF_Box *a, FILE * trace)
{
	GF_XMLBox *p = (GF_XMLBox *)a;
	DumpBox(a, "XMLBox", trace);
	gf_full_box_dump(a, trace);
	fprintf(trace, ">\n");
	fprintf(trace, "<![CDATA[\n");
	if (p->xml_length)
		gf_fwrite(p->xml, p->xml_length, 1, trace);
	fprintf(trace, "]]>\n");
	gf_box_dump_done("XMLBox", a, trace);
	return GF_OK;
}


GF_Err bxml_dump(GF_Box *a, FILE * trace)
{
	GF_BinaryXMLBox *p = (GF_BinaryXMLBox *)a;
	DumpBox(a, "BinaryXMLBox", trace);
	gf_full_box_dump(a, trace);
	fprintf(trace, "binarySize=\"%d\">\n", p->data_length);
	gf_box_dump_done("BinaryXMLBox", a, trace);
	return GF_OK;
}


GF_Err pitm_dump(GF_Box *a, FILE * trace)
{
	GF_PrimaryItemBox *p = (GF_PrimaryItemBox *)a;
	DumpBox(a, "PrimaryItemBox", trace);
	gf_full_box_dump(a, trace);
	fprintf(trace, "item_ID=\"%d\">\n", p->item_ID);
	gf_box_dump_done("PrimaryItemBox", a, trace);
	return GF_OK;
}

GF_Err ipro_dump(GF_Box *a, FILE * trace)
{
	GF_ItemProtectionBox *p = (GF_ItemProtectionBox *)a;
	DumpBox(a, "ItemProtectionBox", trace);
	gf_full_box_dump(a, trace);
	fprintf(trace, ">\n");
	gf_box_array_dump(p->protection_information, trace);
	gf_box_dump_done("ItemProtectionBox", a, trace);
	return GF_OK;
}

GF_Err infe_dump(GF_Box *a, FILE * trace)
{
	GF_ItemInfoEntryBox *p = (GF_ItemInfoEntryBox *)a;
	DumpBox(a, "ItemInfoEntryBox", trace);
	gf_full_box_dump(a, trace);
	fprintf(trace, "item_ID=\"%d\" item_protection_index=\"%d\" item_name=\"%s\" content_type=\"%s\" content_encoding=\"%s\" item_type=\"%s\">\n", p->item_ID, p->item_protection_index, p->item_name, p->content_type, p->content_encoding, gf_4cc_to_str(p->item_type));
	gf_box_dump_done("ItemInfoEntryBox", a, trace);
	return GF_OK;
}

GF_Err iinf_dump(GF_Box *a, FILE * trace)
{
	GF_ItemInfoBox *p = (GF_ItemInfoBox *)a;
	DumpBox(a, "ItemInfoBox", trace);
	gf_full_box_dump(a, trace);
	fprintf(trace, ">\n");
	gf_box_array_dump(p->item_infos, trace);
	gf_box_dump_done("ItemInfoBox", a, trace);
	return GF_OK;
}

GF_Err iloc_dump(GF_Box *a, FILE * trace)
{
	u32 i, j, count, count2;
	GF_ItemLocationBox *p = (GF_ItemLocationBox*)a;
	DumpBox(a, "ItemLocationBox", trace);
	gf_full_box_dump(a, trace);
	fprintf(trace, "offset_size=\"%d\" length_size=\"%d\" base_offset_size=\"%d\" index_size=\"%d\">\n", p->offset_size, p->length_size, p->base_offset_size, p->index_size);
	count = gf_list_count(p->location_entries);
	for (i=0; i<count; i++) {
		GF_ItemLocationEntry *ie = (GF_ItemLocationEntry *)gf_list_get(p->location_entries, i);
		count2 = gf_list_count(ie->extent_entries);
		fprintf(trace, "<ItemLocationEntry item_ID=\"%d\" data_reference_index=\"%d\" base_offset=\""LLD"\" construction_method=\"%d\">\n", ie->item_ID, ie->data_reference_index, LLD_CAST ie->base_offset, ie->construction_method);
		for (j=0; j<count2; j++) {
			GF_ItemExtentEntry *iee = (GF_ItemExtentEntry *)gf_list_get(ie->extent_entries, j);
			fprintf(trace, "<ItemExtentEntry extent_offset=\""LLD"\" extent_length=\""LLD"\" extent_index=\""LLD"\" />\n", LLD_CAST iee->extent_offset, LLD_CAST iee->extent_length, LLD_CAST iee->extent_index);
		}
		fprintf(trace, "</ItemLocationEntry>\n");
	}
	if (!p->size) {
		fprintf(trace, "<ItemLocationEntry item_ID=\"\" data_reference_index=\"\" base_offset=\"\" construction_method=\"\">\n");
		fprintf(trace, "<ItemExtentEntry extent_offset=\"\" extent_length=\"\" extent_index=\"\" />\n");
		fprintf(trace, "</ItemLocationEntry>\n");
	}
	gf_box_dump_done("ItemLocationBox", a, trace);
	return GF_OK;
}

GF_Err iref_dump(GF_Box *a, FILE * trace)
{
	GF_ItemReferenceBox *p = (GF_ItemReferenceBox *)a;
	DumpBox(a, "ItemReferenceBox", trace);
	gf_full_box_dump(a, trace);
	fprintf(trace, ">\n");
	gf_box_array_dump(p->references, trace);
	gf_box_dump_done("ItemReferenceBox", a, trace);
	return GF_OK;
}

GF_Err hinf_dump(GF_Box *a, FILE * trace)
{
//	GF_HintInfoBox *p  = (GF_HintInfoBox *)a;
	DumpBox(a, "HintInfoBox", trace);
	fprintf(trace, ">\n");
	gf_box_dump_done("HintInfoBox", a, trace);
	return GF_OK;
}

GF_Err trpy_dump(GF_Box *a, FILE * trace)
{
	GF_TRPYBox *p = (GF_TRPYBox *)a;
	DumpBox(a, "LargeTotalRTPBytesBox", trace);
	fprintf(trace, "RTPBytesSent=\""LLD"\">\n", LLD_CAST p->nbBytes);
	gf_box_dump_done("LargeTotalRTPBytesBox", a, trace);
	return GF_OK;
}

GF_Err totl_dump(GF_Box *a, FILE * trace)
{
	GF_TOTLBox *p;

	p = (GF_TOTLBox *)a;
	DumpBox(a, "TotalRTPBytesBox", trace);
	fprintf(trace, "RTPBytesSent=\"%d\">\n", p->nbBytes);
	gf_box_dump_done("TotalRTPBytesBox", a, trace);
	return GF_OK;
}

GF_Err nump_dump(GF_Box *a, FILE * trace)
{
	GF_NUMPBox *p;

	p = (GF_NUMPBox *)a;
	DumpBox(a, "LargeTotalPacketBox", trace);
	fprintf(trace, "PacketsSent=\""LLD"\">\n", LLD_CAST p->nbPackets);
	gf_box_dump_done("LargeTotalPacketBox", a, trace);
	return GF_OK;
}

GF_Err npck_dump(GF_Box *a, FILE * trace)
{
	GF_NPCKBox *p;
	p = (GF_NPCKBox *)a;
	DumpBox(a, "TotalPacketBox", trace);
	fprintf(trace, "packetsSent=\"%d\">\n", p->nbPackets);
	gf_box_dump_done("TotalPacketBox", a, trace);
	return GF_OK;
}

GF_Err tpyl_dump(GF_Box *a, FILE * trace)
{
	GF_NTYLBox *p;
	p = (GF_NTYLBox *)a;
	DumpBox(a, "LargeTotalMediaBytesBox", trace);
	fprintf(trace, "BytesSent=\""LLD"\">\n", LLD_CAST p->nbBytes);
	gf_box_dump_done("LargeTotalMediaBytesBox", a, trace);
	return GF_OK;
}

GF_Err tpay_dump(GF_Box *a, FILE * trace)
{
	GF_TPAYBox *p;
	p = (GF_TPAYBox *)a;
	DumpBox(a, "TotalMediaBytesBox", trace);
	fprintf(trace, "BytesSent=\"%d\">\n", p->nbBytes);
	gf_box_dump_done("TotalMediaBytesBox", a, trace);
	return GF_OK;
}

GF_Err maxr_dump(GF_Box *a, FILE * trace)
{
	GF_MAXRBox *p;
	p = (GF_MAXRBox *)a;
	DumpBox(a, "MaxDataRateBox", trace);
	fprintf(trace, "MaxDataRate=\"%d\" Granularity=\"%d\">\n", p->maxDataRate, p->granularity);
	gf_box_dump_done("MaxDataRateBox", a, trace);
	return GF_OK;
}

GF_Err dmed_dump(GF_Box *a, FILE * trace)
{
	GF_DMEDBox *p;

	p = (GF_DMEDBox *)a;
	DumpBox(a, "BytesFromMediaTrackBox", trace);
	fprintf(trace, "BytesSent=\""LLD"\">\n", LLD_CAST p->nbBytes);
	gf_box_dump_done("BytesFromMediaTrackBox", a, trace);
	return GF_OK;
}

GF_Err dimm_dump(GF_Box *a, FILE * trace)
{
	GF_DIMMBox *p;
	p = (GF_DIMMBox *)a;
	DumpBox(a, "ImmediateDataBytesBox", trace);
	fprintf(trace, "BytesSent=\""LLD"\">\n", LLD_CAST p->nbBytes);
	gf_box_dump_done("ImmediateDataBytesBox", a, trace);
	return GF_OK;
}

GF_Err drep_dump(GF_Box *a, FILE * trace)
{
	GF_DREPBox *p;
	p = (GF_DREPBox *)a;
	DumpBox(a, "RepeatedDataBytesBox", trace);
	fprintf(trace, "RepeatedBytes=\""LLD"\">\n", LLD_CAST p->nbBytes);
	gf_box_dump_done("RepeatedDataBytesBox", a, trace);
	return GF_OK;
}

GF_Err tmin_dump(GF_Box *a, FILE * trace)
{
	GF_TMINBox *p;
	p = (GF_TMINBox *)a;
	DumpBox(a, "MinTransmissionTimeBox", trace);
	fprintf(trace, "MinimumTransmitTime=\"%d\">\n", p->minTime);
	gf_box_dump_done("MinTransmissionTimeBox", a, trace);
	return GF_OK;
}

GF_Err tmax_dump(GF_Box *a, FILE * trace)
{
	GF_TMAXBox *p;
	p = (GF_TMAXBox *)a;
	DumpBox(a, "MaxTransmissionTimeBox", trace);
	fprintf(trace, "MaximumTransmitTime=\"%d\">\n", p->maxTime);
	gf_box_dump_done("MaxTransmissionTimeBox", a, trace);
	return GF_OK;
}

GF_Err pmax_dump(GF_Box *a, FILE * trace)
{
	GF_PMAXBox *p;
	p = (GF_PMAXBox *)a;
	DumpBox(a, "MaxPacketSizeBox", trace);
	fprintf(trace, "MaximumSize=\"%d\">\n", p->maxSize);
	gf_box_dump_done("MaxPacketSizeBox", a, trace);
	return GF_OK;
}

GF_Err dmax_dump(GF_Box *a, FILE * trace)
{
	GF_DMAXBox *p;
	p = (GF_DMAXBox *)a;
	DumpBox(a, "MaxPacketDurationBox", trace);
	fprintf(trace, "MaximumDuration=\"%d\">\n", p->maxDur);
	gf_box_dump_done("MaxPacketDurationBox", a, trace);
	return GF_OK;
}

GF_Err payt_dump(GF_Box *a, FILE * trace)
{
	GF_PAYTBox *p;
	p = (GF_PAYTBox *)a;
	DumpBox(a, "PayloadTypeBox", trace);
	fprintf(trace, "PayloadID=\"%d\" PayloadString=\"%s\">\n", p->payloadCode, p->payloadString);
	gf_box_dump_done("PayloadTypeBox", a, trace);
	return GF_OK;
}

GF_Err name_dump(GF_Box *a, FILE * trace)
{
	GF_NameBox *p;
	p = (GF_NameBox *)a;
	DumpBox(a, "NameBox", trace);
	fprintf(trace, "Name=\"%s\">\n", p->string);
	gf_box_dump_done("NameBox", a, trace);
	return GF_OK;
}

GF_Err rely_dump(GF_Box *a, FILE * trace)
{
	GF_RelyHintBox *p;
	p = (GF_RelyHintBox *)a;
	DumpBox(a, "RelyTransmissionBox", trace);
	fprintf(trace, "Prefered=\"%d\" required=\"%d\">\n", p->prefered, p->required);
	gf_box_dump_done("RelyTransmissionBox", a, trace);
	return GF_OK;
}

GF_Err snro_dump(GF_Box *a, FILE * trace)
{
	GF_SeqOffHintEntryBox *p;
	p = (GF_SeqOffHintEntryBox *)a;
	DumpBox(a, "PacketSequenceOffsetBox", trace);
	fprintf(trace, "SeqNumOffset=\"%d\">\n", p->SeqOffset);
	gf_box_dump_done("PacketSequenceOffsetBox", a, trace);
	return GF_OK;
}

GF_Err tims_dump(GF_Box *a, FILE * trace)
{
	GF_TSHintEntryBox *p;
	p = (GF_TSHintEntryBox *)a;
	DumpBox(a, "RTPTimeScaleBox", trace);
	fprintf(trace, "TimeScale=\"%d\">\n", p->timeScale);
	gf_box_dump_done("RTPTimeScaleBox", a, trace);
	return GF_OK;
}

GF_Err tsro_dump(GF_Box *a, FILE * trace)
{
	GF_TimeOffHintEntryBox *p;
	p = (GF_TimeOffHintEntryBox *)a;
	DumpBox(a, "TimeStampOffsetBox", trace);
	fprintf(trace, "TimeStampOffset=\"%d\">\n", p->TimeOffset);
	gf_box_dump_done("TimeStampOffsetBox", a, trace);
	return GF_OK;
}


GF_Err ghnt_dump(GF_Box *a, FILE * trace)
{
	GF_HintSampleEntryBox *p;
	p = (GF_HintSampleEntryBox *)a;
	DumpBox(a, "GenericHintSampleEntryBox", trace);
	fprintf(trace, "EntrySubType=\"%s\" DataReferenceIndex=\"%d\" HintTrackVersion=\"%d\" LastCompatibleVersion=\"%d\" MaxPacketSize=\"%d\">\n",
	        gf_4cc_to_str(p->type), p->dataReferenceIndex, p->HintTrackVersion, p->LastCompatibleVersion, p->MaxPacketSize);

	gf_box_array_dump(p->HintDataTable, trace);
	gf_box_dump_done("GenericHintSampleEntryBox", a, trace);
	return GF_OK;
}

GF_Err hnti_dump(GF_Box *a, FILE * trace)
{
	GF_HintTrackInfoBox *p;
	GF_Box *ptr;
	GF_RTPBox *rtp;
	u32 i;

	p = (GF_HintTrackInfoBox *)a;
	DumpBox(a, "HintTrackInfoBox", trace);
	fprintf(trace, ">\n");

	i=0;
	while ((ptr = (GF_Box *)gf_list_enum(p->other_boxes, &i))) {
		if (ptr->type !=GF_ISOM_BOX_TYPE_RTP) {
			gf_box_dump(ptr, trace);
		} else {
			rtp = (GF_RTPBox *)ptr;
			fprintf(trace, "<RTPInfoBox subType=\"%s\">\n", gf_4cc_to_str(rtp->subType));
			if (rtp->sdpText) fprintf(trace, "<!-- sdp text: %s -->\n", rtp->sdpText);
			gf_box_dump_done("RTPInfoBox", a, trace);
		}
	}
	gf_box_dump_done("HintTrackInfoBox", NULL, trace);
	return GF_OK;
}

GF_Err sdp_dump(GF_Box *a, FILE * trace)
{
	GF_SDPBox *p;
	p = (GF_SDPBox *)a;
	DumpBox(a, "SDPBox", trace);
	fprintf(trace, ">\n");
	if (p->sdpText)
		fprintf(trace, "<!-- sdp text: %s -->\n", p->sdpText);
	gf_box_dump_done("SDPBox", a, trace);
	return GF_OK;
}

GF_Err rtpo_dump(GF_Box *a, FILE * trace)
{
	GF_RTPOBox *p;
	p = (GF_RTPOBox *)a;
	DumpBox(a, "RTPTimeOffsetBox", trace);
	fprintf(trace, "PacketTimeOffset=\"%d\">\n", p->timeOffset);
	gf_box_dump_done("RTPTimeOffsetBox", a, trace);
	return GF_OK;
}

#ifndef	GPAC_DISABLE_ISOM_FRAGMENTS

GF_Err mvex_dump(GF_Box *a, FILE * trace)
{
	GF_MovieExtendsBox *p;
	p = (GF_MovieExtendsBox *)a;
	DumpBox(a, "MovieExtendsBox", trace);
	fprintf(trace, ">\n");
	if (p->mehd) gf_box_dump(p->mehd, trace);
	gf_box_array_dump(p->TrackExList, trace);
	gf_box_array_dump(p->TrackExPropList, trace);
	gf_box_dump_done("MovieExtendsBox", a, trace);
	return GF_OK;
}

GF_Err mehd_dump(GF_Box *a, FILE * trace)
{
	GF_MovieExtendsHeaderBox *p = (GF_MovieExtendsHeaderBox*)a;
	DumpBox(a, "MovieExtendsHeaderBox", trace);
	gf_full_box_dump(a, trace);
	fprintf(trace, "fragmentDuration=\""LLD"\" >\n", LLD_CAST p->fragment_duration);
	gf_box_dump_done("MovieExtendsHeaderBox", a, trace);
	return GF_OK;
}

void sample_flags_dump(const char *name, u32 sample_flags, FILE * trace)
{
	fprintf(trace, "<%s", name);
	fprintf(trace, " IsLeading=\"%d\"", GF_ISOM_GET_FRAG_LEAD(sample_flags) );
	fprintf(trace, " SampleDependsOn=\"%d\"", GF_ISOM_GET_FRAG_DEPENDS(sample_flags) );
	fprintf(trace, " SampleIsDependedOn=\"%d\"", GF_ISOM_GET_FRAG_DEPENDED(sample_flags) );
	fprintf(trace, " SampleHasRedundancy=\"%d\"", GF_ISOM_GET_FRAG_REDUNDANT(sample_flags) );
	fprintf(trace, " SamplePadding=\"%d\"", GF_ISOM_GET_FRAG_PAD(sample_flags) );
	fprintf(trace, " SampleSync=\"%d\"", GF_ISOM_GET_FRAG_SYNC(sample_flags));
	fprintf(trace, " SampleDegradationPriority=\"%d\"", GF_ISOM_GET_FRAG_DEG(sample_flags));
	fprintf(trace, "/>\n");
}

GF_Err trex_dump(GF_Box *a, FILE * trace)
{
	GF_TrackExtendsBox *p;
	p = (GF_TrackExtendsBox *)a;
	DumpBox(a, "TrackExtendsBox", trace);
	gf_full_box_dump(a, trace);
	fprintf(trace, "TrackID=\"%d\"", p->trackID);
	fprintf(trace, " SampleDescriptionIndex=\"%d\" SampleDuration=\"%d\" SampleSize=\"%d\"", p->def_sample_desc_index, p->def_sample_duration, p->def_sample_size);
	fprintf(trace, ">\n");
	sample_flags_dump("DefaultSampleFlags", p->def_sample_flags, trace);
	gf_box_dump_done("TrackExtendsBox", a, trace);
	return GF_OK;
}

GF_Err trep_dump(GF_Box *a, FILE * trace)
{
	GF_TrackExtensionPropertiesBox *p = (GF_TrackExtensionPropertiesBox*)a;
	DumpBox(a, "TrackExtensionPropertiesBox", trace);
	gf_full_box_dump(a, trace);
	fprintf(trace, "TrackID=\"%d\">\n", p->trackID);
	gf_box_dump_done("TrackExtensionPropertiesBox", a, trace);
	return GF_OK;
}

GF_Err moof_dump(GF_Box *a, FILE * trace)
{
	GF_MovieFragmentBox *p;
	p = (GF_MovieFragmentBox *)a;
	DumpBox(a, "MovieFragmentBox", trace);
	fprintf(trace, "TrackFragments=\"%d\">\n", gf_list_count(p->TrackList));
	if (p->mfhd) gf_box_dump(p->mfhd, trace);
	gf_box_array_dump(p->TrackList, trace);
	gf_box_dump_done("MovieFragmentBox", a, trace);
	return GF_OK;
}

GF_Err mfhd_dump(GF_Box *a, FILE * trace)
{
	GF_MovieFragmentHeaderBox *p;
	p = (GF_MovieFragmentHeaderBox *)a;
	DumpBox(a, "MovieFragmentHeaderBox", trace);
	gf_full_box_dump(a, trace);
	fprintf(trace, "FragmentSequenceNumber=\"%d\">\n", p->sequence_number);
	gf_box_dump_done("MovieFragmentHeaderBox", a, trace);
	return GF_OK;
}

GF_Err traf_dump(GF_Box *a, FILE * trace)
{
	GF_TrackFragmentBox *p;
	p = (GF_TrackFragmentBox *)a;
	DumpBox(a, "TrackFragmentBox", trace);
	fprintf(trace, ">\n");
	if (p->tfhd) gf_box_dump(p->tfhd, trace);
	if (p->sdtp) gf_box_dump(p->sdtp, trace);
	if (p->tfdt) gf_box_dump(p->tfdt, trace);
	if (p->sub_samples) gf_box_array_dump(p->sub_samples, trace);
	if (p->sampleGroupsDescription) gf_box_array_dump(p->sampleGroupsDescription, trace);
	if (p->sampleGroups) gf_box_array_dump(p->sampleGroups, trace);
	gf_box_array_dump(p->TrackRuns, trace);
	if (p->sai_sizes) gf_box_array_dump(p->sai_sizes, trace);
	if (p->sai_offsets) gf_box_array_dump(p->sai_offsets, trace);
	if (p->piff_sample_encryption) gf_box_dump(p->piff_sample_encryption, trace);
	if (p->sample_encryption) gf_box_dump(p->sample_encryption, trace);
	gf_box_dump_done("TrackFragmentBox", a, trace);
	return GF_OK;
}

static void frag_dump_sample_flags(FILE * trace, u32 flags)
{
	fprintf(trace, " SamplePadding=\"%d\" Sync=\"%d\" DegradationPriority=\"%d\" IsLeading=\"%d\" DependsOn=\"%d\" IsDependedOn=\"%d\" HasRedundancy=\"%d\"",
	        GF_ISOM_GET_FRAG_PAD(flags), GF_ISOM_GET_FRAG_SYNC(flags), GF_ISOM_GET_FRAG_DEG(flags),
	        GF_ISOM_GET_FRAG_LEAD(flags), GF_ISOM_GET_FRAG_DEPENDS(flags), GF_ISOM_GET_FRAG_DEPENDED(flags), GF_ISOM_GET_FRAG_REDUNDANT(flags));
}

GF_Err tfhd_dump(GF_Box *a, FILE * trace)
{
	GF_TrackFragmentHeaderBox *p;
	p = (GF_TrackFragmentHeaderBox *)a;
	DumpBox(a, "TrackFragmentHeaderBox", trace);
	gf_full_box_dump(a, trace);
	fprintf(trace, "TrackID=\"%u\"", p->trackID);

	if (p->flags & GF_ISOM_TRAF_BASE_OFFSET) {
		fprintf(trace, " BaseDataOffset=\""LLU"\"", p->base_data_offset);
	} else {
		fprintf(trace, " BaseDataOffset=\"%s\"", (p->flags & GF_ISOM_MOOF_BASE_OFFSET) ? "moof" : "moof-or-previous-traf");
	}

	if (p->flags & GF_ISOM_TRAF_SAMPLE_DESC)
		fprintf(trace, " SampleDescriptionIndex=\"%u\"", p->sample_desc_index);
	if (p->flags & GF_ISOM_TRAF_SAMPLE_DUR)
		fprintf(trace, " SampleDuration=\"%u\"", p->def_sample_duration);
	if (p->flags & GF_ISOM_TRAF_SAMPLE_SIZE)
		fprintf(trace, " SampleSize=\"%u\"", p->def_sample_size);

	if (p->flags & GF_ISOM_TRAF_SAMPLE_FLAGS) {
		frag_dump_sample_flags(trace, p->def_sample_flags);
	}

	fprintf(trace, ">\n");

	gf_box_dump_done("TrackFragmentHeaderBox", a, trace);
	return GF_OK;
}

GF_Err tfxd_dump(GF_Box *a, FILE * trace)
{
	GF_MSSTimeExtBox *ptr = (GF_MSSTimeExtBox*)a;
	if (!a) return GF_BAD_PARAM;
	DumpBox(a, "MSSTimeExtensionBox", trace);
	fprintf(trace, "AbsoluteTime=\""LLU"\" FragmentDuration=\""LLU"\">\n", ptr->absolute_time_in_track_timescale, ptr->fragment_duration_in_track_timescale);
	fprintf(trace, "<FullBoxInfo Version=\"%d\" Flags=\"%d\"/>\n", ptr->version, ptr->flags);
	gf_box_dump_done("MSSTimeExtensionBox", a, trace);
	return GF_OK;
}

GF_Err trun_dump(GF_Box *a, FILE * trace)
{
	u32 i;
	GF_TrunEntry *ent;
	GF_TrackFragmentRunBox *p;

	p = (GF_TrackFragmentRunBox *)a;
	DumpBox(a, "TrackRunBox", trace);
	gf_full_box_dump(a, trace);
	fprintf(trace, "SampleCount=\"%d\"", p->sample_count);

	if (p->flags & GF_ISOM_TRUN_DATA_OFFSET)
		fprintf(trace, " DataOffset=\"%d\"", p->data_offset);
	fprintf(trace, ">\n");

	if (p->flags & GF_ISOM_TRUN_FIRST_FLAG) {
		sample_flags_dump("FirstSampleFlags", p->first_sample_flags, trace);
	}

	if (p->flags & (GF_ISOM_TRUN_DURATION|GF_ISOM_TRUN_SIZE|GF_ISOM_TRUN_CTS_OFFSET|GF_ISOM_TRUN_FLAGS)) {
		i=0;
		while ((ent = (GF_TrunEntry *)gf_list_enum(p->entries, &i))) {

			fprintf(trace, "<TrackRunEntry");

			if (p->flags & GF_ISOM_TRUN_DURATION)
				fprintf(trace, " Duration=\"%u\"", ent->Duration);
			if (p->flags & GF_ISOM_TRUN_SIZE)
				fprintf(trace, " Size=\"%u\"", ent->size);
			if (p->flags & GF_ISOM_TRUN_CTS_OFFSET)
			{
				if (p->version == 0)
					fprintf(trace, " CTSOffset=\"%u\"", (u32) ent->CTS_Offset);
				else
					fprintf(trace, " CTSOffset=\"%d\"", ent->CTS_Offset);
			}

			if (p->flags & GF_ISOM_TRUN_FLAGS) {
				frag_dump_sample_flags(trace, ent->flags);
			}
			fprintf(trace, "/>\n");
		}
	} else if (p->size) {
		fprintf(trace, "<!-- all default values used -->\n");
	} else {
		fprintf(trace, "<TrackRunEntry Duration=\"\" Size=\"\" CTSOffset=\"\"");
		frag_dump_sample_flags(trace, 0);
		fprintf(trace, "/>\n");
	}

	gf_box_dump_done("TrackRunBox", a, trace);
	return GF_OK;
}

#endif

#ifndef GPAC_DISABLE_ISOM_HINTING

GF_Err DTE_Dump(GF_List *dte, FILE * trace)
{
	GF_GenericDTE *p;
	GF_ImmediateDTE *i_p;
	GF_SampleDTE *s_p;
	GF_StreamDescDTE *sd_p;
	u32 i, count;

	count = gf_list_count(dte);
	for (i=0; i<count; i++) {
		p = (GF_GenericDTE *)gf_list_get(dte, i);
		switch (p->source) {
		case 0:
			fprintf(trace, "<EmptyDataEntry/>\n");
			break;
		case 1:
			i_p = (GF_ImmediateDTE *) p;
			fprintf(trace, "<ImmediateDataEntry DataSize=\"%d\"/>\n", i_p->dataLength);
			break;
		case 2:
			s_p = (GF_SampleDTE *) p;
			fprintf(trace, "<SampleDataEntry DataSize=\"%d\" SampleOffset=\"%d\" SampleNumber=\"%d\" TrackReference=\"%d\"/>\n",
			        s_p->dataLength, s_p->byteOffset, s_p->sampleNumber, s_p->trackRefIndex);
			break;
		case 3:
			sd_p = (GF_StreamDescDTE *) p;
			fprintf(trace, "<SampleDescriptionEntry DataSize=\"%d\" DescriptionOffset=\"%d\" StreamDescriptionindex=\"%d\" TrackReference=\"%d\"/>\n",
			        sd_p->dataLength, sd_p->byteOffset, sd_p->streamDescIndex, sd_p->trackRefIndex);
			break;
		default:
			fprintf(trace, "<UnknownTableEntry/>\n");
			break;
		}
	}
	return GF_OK;
}


GF_EXPORT
GF_Err gf_isom_dump_hint_sample(GF_ISOFile *the_file, u32 trackNumber, u32 SampleNum, FILE * trace)
{
	GF_ISOSample *tmp;
	GF_HintSampleEntryBox *entry;
	u32 descIndex, count, count2, i;
	GF_Err e;
	GF_BitStream *bs;
	GF_HintSample *s;
	GF_TrackBox *trak;
	GF_RTPPacket *pck;

	trak = gf_isom_get_track_from_file(the_file, trackNumber);
	if (!trak || !IsHintTrack(trak)) return GF_BAD_PARAM;

	tmp = gf_isom_get_sample(the_file, trackNumber, SampleNum, &descIndex);
	if (!tmp) return GF_BAD_PARAM;

	e = Media_GetSampleDesc(trak->Media, descIndex, (GF_SampleEntryBox **) &entry, &count);
	if (e) {
		gf_isom_sample_del(&tmp);
		return e;
	}

	//check we can read the sample
	switch (entry->type) {
	case GF_ISOM_BOX_TYPE_RTP_STSD:
		break;
	default:
		gf_isom_sample_del(&tmp);
		return GF_NOT_SUPPORTED;
	}

	bs = gf_bs_new(tmp->data, tmp->dataLength, GF_BITSTREAM_READ);
	s = gf_isom_hint_sample_new(entry->type);
	gf_isom_hint_sample_read(s, bs, tmp->dataLength);
	gf_bs_del(bs);

	count = gf_list_count(s->packetTable);

	fprintf(trace, "<RTPHintSample SampleNumber=\"%d\" DecodingTime=\""LLD"\" RandomAccessPoint=\"%d\" PacketCount=\"%u\">\n", SampleNum, LLD_CAST tmp->DTS, tmp->IsRAP, count);

	for (i=0; i<count; i++) {
		pck = (GF_RTPPacket *)gf_list_get(s->packetTable, i);

		fprintf(trace, "<RTPHintPacket PacketNumber=\"%d\" P=\"%d\" X=\"%d\" M=\"%d\" PayloadType=\"%d\"",
		        i+1,  pck->P_bit, pck->X_bit, pck->M_bit, pck->payloadType);

		fprintf(trace, " SequenceNumber=\"%d\" RepeatedPacket=\"%d\" DropablePacket=\"%d\" RelativeTransmissionTime=\"%d\" FullPacketSize=\"%d\">\n",
		        pck->SequenceNumber, pck->R_bit, pck->B_bit, pck->relativeTransTime, gf_isom_hint_rtp_length(pck));


		//TLV is made of Boxes
		count2 = gf_list_count(pck->TLV);
		if (count2) {
			fprintf(trace, "<PrivateExtensionTable EntryCount=\"%d\">\n", count2);
			gf_box_array_dump(pck->TLV, trace);
			fprintf(trace, "</PrivateExtensionTable>\n");
		}
		//DTE is made of NON boxes
		count2 = gf_list_count(pck->DataTable);
		if (count2) {
			fprintf(trace, "<PacketDataTable EntryCount=\"%d\">\n", count2);
			DTE_Dump(pck->DataTable, trace);
			fprintf(trace, "</PacketDataTable>\n");
		}
		fprintf(trace, "</RTPHintPacket>\n");
	}

	fprintf(trace, "</RTPHintSample>\n");
	gf_isom_sample_del(&tmp);
	gf_isom_hint_sample_del(s);
	return GF_OK;
}

#endif /*GPAC_DISABLE_ISOM_HINTING*/

static void gpp_dump_box_nobox(FILE * trace, GF_BoxRecord *rec)
{
	fprintf(trace, "<TextBox top=\"%d\" left=\"%d\" bottom=\"%d\" right=\"%d\"/>\n", rec->top, rec->left, rec->bottom, rec->right);
}

static void gpp_print_char_offsets(FILE * trace, u32 start, u32 end, u32 *shift_offset, u32 so_count)
{
	u32 i;
	if (shift_offset) {
		for (i=0; i<so_count; i++) {
			if (start>shift_offset[i]) {
				start --;
				break;
			}
		}
		for (i=0; i<so_count; i++) {
			if (end>shift_offset[i]) {
				end --;
				break;
			}
		}
	}
	if (start || end) fprintf(trace, "fromChar=\"%d\" toChar=\"%d\" ", start, end);
}

static void gpp_dump_style_nobox(FILE * trace, GF_StyleRecord *rec, u32 *shift_offset, u32 so_count)
{
	fprintf(trace, "<Style ");
	if (rec->startCharOffset || rec->endCharOffset)
		gpp_print_char_offsets(trace, rec->startCharOffset, rec->endCharOffset, shift_offset, so_count);

	fprintf(trace, "styles=\"");
	if (!rec->style_flags) {
		fprintf(trace, "Normal");
	} else {
		if (rec->style_flags & 1) fprintf(trace, "Bold ");
		if (rec->style_flags & 2) fprintf(trace, "Italic ");
		if (rec->style_flags & 4) fprintf(trace, "Underlined ");
	}
	fprintf(trace, "\" fontID=\"%d\" fontSize=\"%d\" ", rec->fontID, rec->font_size);
	gpp_dump_rgba8(trace, "color", rec->text_color);
	fprintf(trace, "/>\n");
}

static char *ttd_format_time(u64 ts, u32 timescale, char *szDur, Bool is_srt)
{
	u32 h, m, s, ms;
	ts = (u32) (ts*1000 / timescale);
	h = (u32) (ts / 3600000);
	m = (u32) (ts/ 60000) - h*60;
	s = (u32) (ts/1000) - h*3600 - m*60;
	ms = (u32) (ts) - h*3600000 - m*60000 - s*1000;
	if (is_srt) {
		sprintf(szDur, "%02d:%02d:%02d,%03d", h, m, s, ms);
	} else {
		sprintf(szDur, "%02d:%02d:%02d.%03d", h, m, s, ms);
	}
	return szDur;
}

//#define DUMP_OLD_TEXT

static GF_Err gf_isom_dump_ttxt_track(GF_ISOFile *the_file, u32 track, FILE *dump)
{
	u32 i, j, count, di, nb_descs, shift_offset[20], so_count;
	u64 last_DTS;
	size_t len;
	GF_Box *a;
	Bool has_scroll;
	char szDur[100];
	GF_Tx3gSampleEntryBox *txt;

	GF_TrackBox *trak = gf_isom_get_track_from_file(the_file, track);
	if (!trak) return GF_BAD_PARAM;
	switch (trak->Media->handler->handlerType) {
	case GF_ISOM_MEDIA_TEXT:
	case GF_ISOM_MEDIA_SUBT:
		break;
	default:
		return GF_BAD_PARAM;
	}

	txt = (GF_Tx3gSampleEntryBox *)gf_list_get(trak->Media->information->sampleTable->SampleDescription->other_boxes, 0);
	switch (txt->type) {
	case GF_ISOM_BOX_TYPE_TX3G:
	case GF_ISOM_BOX_TYPE_TEXT:
		break;
	case GF_ISOM_BOX_TYPE_STPP:
	case GF_ISOM_BOX_TYPE_SBTT:
	default:
		return GF_BAD_PARAM;
	}

	fprintf(dump, "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n");
	fprintf(dump, "<!-- GPAC 3GPP Text Stream -->\n");

#ifdef DUMP_OLD_TEXT
	fprintf(dump, "<TextStream version=\"1.0\">\n");
#else
	fprintf(dump, "<TextStream version=\"1.1\">\n");
#endif
	fprintf(dump, "<TextStreamHeader width=\"%d\" height=\"%d\" layer=\"%d\" translation_x=\"%d\" translation_y=\"%d\">\n", trak->Header->width >> 16 , trak->Header->height >> 16, trak->Header->layer, trak->Header->matrix[6] >> 16, trak->Header->matrix[7] >> 16);

	nb_descs = gf_list_count(trak->Media->information->sampleTable->SampleDescription->other_boxes);
	for (i=0; i<nb_descs; i++) {
		GF_Tx3gSampleEntryBox *txt = (GF_Tx3gSampleEntryBox *)gf_list_get(trak->Media->information->sampleTable->SampleDescription->other_boxes, i);

		if (txt->type==GF_ISOM_BOX_TYPE_TX3G) {
			fprintf(dump, "<TextSampleDescription horizontalJustification=\"");
			switch (txt->horizontal_justification) {
			case 1:
				fprintf(dump, "center");
				break;
			case -1:
				fprintf(dump, "right");
				break;
			default:
				fprintf(dump, "left");
				break;
			}
			fprintf(dump, "\" verticalJustification=\"");
			switch (txt->vertical_justification) {
			case 1:
				fprintf(dump, "center");
				break;
			case -1:
				fprintf(dump, "bottom");
				break;
			default:
				fprintf(dump, "top");
				break;
			}
			fprintf(dump, "\" ");
			gpp_dump_rgba8(dump, "backColor", txt->back_color);
			fprintf(dump, " verticalText=\"%s\"", (txt->displayFlags & GF_TXT_VERTICAL) ? "yes" : "no");
			fprintf(dump, " fillTextRegion=\"%s\"", (txt->displayFlags & GF_TXT_FILL_REGION) ? "yes" : "no");
			fprintf(dump, " continuousKaraoke=\"%s\"", (txt->displayFlags & GF_TXT_KARAOKE) ? "yes" : "no");
			has_scroll = GF_FALSE;
			if (txt->displayFlags & GF_TXT_SCROLL_IN) {
				has_scroll = GF_TRUE;
				if (txt->displayFlags & GF_TXT_SCROLL_OUT) fprintf(dump, " scroll=\"InOut\"");
				else fprintf(dump, " scroll=\"In\"");
			} else if (txt->displayFlags & GF_TXT_SCROLL_OUT) {
				has_scroll = GF_TRUE;
				fprintf(dump, " scroll=\"Out\"");
			} else {
				fprintf(dump, " scroll=\"None\"");
			}
			if (has_scroll) {
				u32 mode = (txt->displayFlags & GF_TXT_SCROLL_DIRECTION)>>7;
				switch (mode) {
				case GF_TXT_SCROLL_CREDITS:
					fprintf(dump, " scrollMode=\"Credits\"");
					break;
				case GF_TXT_SCROLL_MARQUEE:
					fprintf(dump, " scrollMode=\"Marquee\"");
					break;
				case GF_TXT_SCROLL_DOWN:
					fprintf(dump, " scrollMode=\"Down\"");
					break;
				case GF_TXT_SCROLL_RIGHT:
					fprintf(dump, " scrollMode=\"Right\"");
					break;
				default:
					fprintf(dump, " scrollMode=\"Unknown\"");
					break;
				}
			}
			fprintf(dump, ">\n");
			fprintf(dump, "<FontTable>\n");
			if (txt->font_table) {
				for (j=0; j<txt->font_table->entry_count; j++) {
					fprintf(dump, "<FontTableEntry fontName=\"%s\" fontID=\"%d\"/>\n", txt->font_table->fonts[j].fontName, txt->font_table->fonts[j].fontID);

				}
			}
			fprintf(dump, "</FontTable>\n");
			if ((txt->default_box.bottom == txt->default_box.top) || (txt->default_box.right == txt->default_box.left)) {
				txt->default_box.top = txt->default_box.left = 0;
				txt->default_box.right = trak->Header->width / 65536;
				txt->default_box.bottom = trak->Header->height / 65536;
			}
			gpp_dump_box_nobox(dump, &txt->default_box);
			gpp_dump_style_nobox(dump, &txt->default_style, NULL, 0);
			fprintf(dump, "</TextSampleDescription>\n");
		} else {
			GF_TextSampleEntryBox *text = (GF_TextSampleEntryBox *)gf_list_get(trak->Media->information->sampleTable->SampleDescription->other_boxes, i);
			fprintf(dump, "<TextSampleDescription horizontalJustification=\"");
			switch (text->textJustification) {
			case 1:
				fprintf(dump, "center");
				break;
			case -1:
				fprintf(dump, "right");
				break;
			default:
				fprintf(dump, "left");
				break;
			}
			fprintf(dump, "\"");

			gpp_dump_rgb16(dump, " backColor", text->background_color);

			if ((text->default_box.bottom == text->default_box.top) || (text->default_box.right == text->default_box.left)) {
				text->default_box.top = text->default_box.left = 0;
				text->default_box.right = trak->Header->width / 65536;
				text->default_box.bottom = trak->Header->height / 65536;
			}

			if (text->displayFlags & GF_TXT_SCROLL_IN) {
				if (text->displayFlags & GF_TXT_SCROLL_OUT) fprintf(dump, " scroll=\"InOut\"");
				else fprintf(dump, " scroll=\"In\"");
			} else if (text->displayFlags & GF_TXT_SCROLL_OUT) {
				fprintf(dump, " scroll=\"Out\"");
			} else {
				fprintf(dump, " scroll=\"None\"");
			}
			fprintf(dump, ">\n");

			gpp_dump_box_nobox(dump, &text->default_box);
			fprintf(dump, "</TextSampleDescription>\n");
		}
	}
	fprintf(dump, "</TextStreamHeader>\n");

	last_DTS = 0;
	count = gf_isom_get_sample_count(the_file, track);
	for (i=0; i<count; i++) {
		GF_BitStream *bs;
		GF_TextSample *txt;
		GF_ISOSample *s = gf_isom_get_sample(the_file, track, i+1, &di);
		if (!s) continue;

		fprintf(dump, "<TextSample sampleTime=\"%s\"", ttd_format_time(s->DTS, trak->Media->mediaHeader->timeScale, szDur, GF_FALSE));
		if (nb_descs>1) fprintf(dump, " sampleDescriptionIndex=\"%d\"", di);

		bs = gf_bs_new(s->data, s->dataLength, GF_BITSTREAM_READ);
		txt = gf_isom_parse_texte_sample(bs);
		gf_bs_del(bs);


		if (txt->highlight_color) {
			fprintf(dump, " ");
			gpp_dump_rgba8(dump, "highlightColor", txt->highlight_color->hil_color);
		}
		if (txt->scroll_delay) {
			Double delay = txt->scroll_delay->scroll_delay;
			delay /= trak->Media->mediaHeader->timeScale;
			fprintf(dump, " scrollDelay=\"%g\"", delay);
		}
		if (txt->wrap) fprintf(dump, " wrap=\"%s\"", (txt->wrap->wrap_flag==0x01) ? "Automatic" : "None");

		so_count = 0;

#ifndef DUMP_OLD_TEXT
		fprintf(dump, " xml:space=\"preserve\">");
#endif
		if (!txt->len) {
#ifdef DUMP_OLD_TEXT
			fprintf(dump, " text=\"\"");
#endif
			last_DTS = (u32) trak->Media->mediaHeader->duration;
		} else {
			unsigned short utf16Line[10000];
			last_DTS = s->DTS;
			/*UTF16*/
			if ((txt->len>2) && ((unsigned char) txt->text[0] == (unsigned char) 0xFE) && ((unsigned char) txt->text[1] == (unsigned char) 0xFF)) {
				/*copy 2 more chars because the lib always add 2 '0' at the end for UTF16 end of string*/
				memcpy((char *) utf16Line, txt->text+2, sizeof(char) * (txt->len));
				len = gf_utf8_wcslen((const u16*)utf16Line);
			} else {
				char *str;
				str = txt->text;
				len = gf_utf8_mbstowcs((u16*)utf16Line, 10000, (const char **) &str);
			}
			if (len != (size_t) -1) {
				utf16Line[len] = 0;
#ifdef DUMP_OLD_TEXT
				fprintf(dump, " text=\"\'");
#endif
				for (j=0; j<len; j++) {
					if ((utf16Line[j]=='\n') || (utf16Line[j]=='\r') || (utf16Line[j]==0x85) || (utf16Line[j]==0x2028) || (utf16Line[j]==0x2029) ) {
#ifndef DUMP_OLD_TEXT
						fprintf(dump, "\n");
#else
						fprintf(dump, "\'\'");
#endif
						if ((utf16Line[j]=='\r') && (utf16Line[j+1]=='\n')) {
							shift_offset[so_count] = j;
							so_count++;
							j++;
						}
					}
					else {
						switch (utf16Line[j]) {
						case '\'':
							fprintf(dump, "&apos;");
							break;
						case '\"':
							fprintf(dump, "&quot;");
							break;
						case '&':
							fprintf(dump, "&amp;");
							break;
						case '>':
							fprintf(dump, "&gt;");
							break;
						case '<':
							fprintf(dump, "&lt;");
							break;
						default:
							if (utf16Line[j] < 128) {
								fprintf(dump, "%c", (u8) utf16Line[j]);
							} else {
								fprintf(dump, "&#%d;", utf16Line[j]);
							}
							break;
						}
					}
				}
#ifdef DUMP_OLD_TEXT
				fprintf(dump, "\'\"");
#endif
			} else {
#ifdef DUMP_OLD_TEXT
				fprintf(dump, "text=\"%s\"", txt->text);
#endif
			}
		}

#ifdef DUMP_OLD_TEXT
		fprintf(dump, ">\n");
#endif

		if (txt->box) gpp_dump_box_nobox(dump, &txt->box->box);
		if (txt->styles) {
			for (j=0; j<txt->styles->entry_count; j++) {
				gpp_dump_style_nobox(dump, &txt->styles->styles[j], shift_offset, so_count);
			}
		}
		j=0;
		while ((a = (GF_Box *)gf_list_enum(txt->others, &j))) {
			switch (a->type) {
			case GF_ISOM_BOX_TYPE_HLIT:
				fprintf(dump, "<Highlight ");
				gpp_print_char_offsets(dump, ((GF_TextHighlightBox *)a)->startcharoffset, ((GF_TextHighlightBox *)a)->endcharoffset, shift_offset, so_count);
				fprintf(dump, "/>\n");
				break;
			case GF_ISOM_BOX_TYPE_HREF:
			{
				GF_TextHyperTextBox *ht = (GF_TextHyperTextBox *)a;
				fprintf(dump, "<HyperLink ");
				gpp_print_char_offsets(dump, ht->startcharoffset, ht->endcharoffset, shift_offset, so_count);
				fprintf(dump, "URL=\"%s\" URLToolTip=\"%s\"/>\n", ht->URL ? ht->URL : "", ht->URL_hint ? ht->URL_hint : "");
			}
			break;
			case GF_ISOM_BOX_TYPE_BLNK:
				fprintf(dump, "<Blinking ");
				gpp_print_char_offsets(dump, ((GF_TextBlinkBox *)a)->startcharoffset, ((GF_TextBlinkBox *)a)->endcharoffset, shift_offset, so_count);
				fprintf(dump, "/>\n");
				break;
			case GF_ISOM_BOX_TYPE_KROK:
			{
				u32 k;
				Double t;
				GF_TextKaraokeBox *krok = (GF_TextKaraokeBox *)a;
				t = krok->highlight_starttime;
				t /= trak->Media->mediaHeader->timeScale;
				fprintf(dump, "<Karaoke startTime=\"%g\">\n", t);
				for (k=0; k<krok->nb_entries; k++) {
					t = krok->records[k].highlight_endtime;
					t /= trak->Media->mediaHeader->timeScale;
					fprintf(dump, "<KaraokeRange ");
					gpp_print_char_offsets(dump, krok->records[k].start_charoffset, krok->records[k].end_charoffset, shift_offset, so_count);
					fprintf(dump, "endTime=\"%g\"/>\n", t);
				}
				fprintf(dump, "</Karaoke>\n");
			}
			break;
			}
		}

		fprintf(dump, "</TextSample>\n");
		gf_isom_sample_del(&s);
		gf_isom_delete_text_sample(txt);
		gf_set_progress("TTXT Extract", i, count);
	}
	if (last_DTS < trak->Media->mediaHeader->duration) {
		fprintf(dump, "<TextSample sampleTime=\"%s\" text=\"\" />\n", ttd_format_time(trak->Media->mediaHeader->duration, trak->Media->mediaHeader->timeScale, szDur, GF_FALSE));
	}

	fprintf(dump, "</TextStream>\n");
	if (count) gf_set_progress("TTXT Extract", count, count);
	return GF_OK;
}

static GF_Err gf_isom_dump_srt_track(GF_ISOFile *the_file, u32 track, FILE *dump)
{
	u32 i, j, k, count, di, len, ts, cur_frame;
	u64 start, end;
	GF_Tx3gSampleEntryBox *txtd;
	GF_BitStream *bs;
	char szDur[100];

	GF_TrackBox *trak = gf_isom_get_track_from_file(the_file, track);
	if (!trak) return GF_BAD_PARAM;
	switch (trak->Media->handler->handlerType) {
	case GF_ISOM_MEDIA_TEXT:
	case GF_ISOM_MEDIA_SUBT:
		break;
	default:
		return GF_BAD_PARAM;
	}

	ts = trak->Media->mediaHeader->timeScale;
	cur_frame = 0;
	end = 0;

	count = gf_isom_get_sample_count(the_file, track);
	for (i=0; i<count; i++) {
		GF_TextSample *txt;
		GF_ISOSample *s = gf_isom_get_sample(the_file, track, i+1, &di);
		if (!s) continue;

		start = s->DTS;
		if (s->dataLength==2) {
			gf_isom_sample_del(&s);
			continue;
		}
		if (i+1<count) {
			GF_ISOSample *next = gf_isom_get_sample_info(the_file, track, i+2, NULL, NULL);
			if (next) {
				end = next->DTS;
				gf_isom_sample_del(&next);
			}
		} else {
			end = gf_isom_get_media_duration(the_file, track) ;
		}
		cur_frame++;
		fprintf(dump, "%d\n", cur_frame);
		ttd_format_time(start, ts, szDur, GF_TRUE);
		fprintf(dump, "%s --> ", szDur);
		ttd_format_time(end, ts, szDur, GF_TRUE);
		fprintf(dump, "%s\n", szDur);

		bs = gf_bs_new(s->data, s->dataLength, GF_BITSTREAM_READ);
		txt = gf_isom_parse_texte_sample(bs);
		gf_bs_del(bs);

		txtd = (GF_Tx3gSampleEntryBox *)gf_list_get(trak->Media->information->sampleTable->SampleDescription->other_boxes, di-1);

		if (!txt->len) {
			fprintf(dump, "\n");
		} else {
			u32 styles, char_num, new_styles, color, new_color;
			u16 utf16Line[10000];

			/*UTF16*/
			if ((txt->len>2) && ((unsigned char) txt->text[0] == (unsigned char) 0xFE) && ((unsigned char) txt->text[1] == (unsigned char) 0xFF)) {
				memcpy(utf16Line, txt->text+2, sizeof(char)*txt->len);
				( ((char *)utf16Line)[txt->len] ) = 0;
				len = txt->len;
			} else {
				u8 *str = (u8 *) (txt->text);
				size_t res = gf_utf8_mbstowcs(utf16Line, 10000, (const char **) &str);
				if (res==(size_t)-1) return GF_NON_COMPLIANT_BITSTREAM;
				len = (u32) res;
				utf16Line[len] = 0;
			}
			char_num = 0;
			styles = 0;
			new_styles = txtd->default_style.style_flags;
			color = new_color = txtd->default_style.text_color;

			for (j=0; j<len; j++) {
				Bool is_new_line;

				if (txt->styles) {
					new_styles = txtd->default_style.style_flags;
					new_color = txtd->default_style.text_color;
					for (k=0; k<txt->styles->entry_count; k++) {
						if (txt->styles->styles[k].startCharOffset>char_num) continue;
						if (txt->styles->styles[k].endCharOffset<char_num+1) continue;

						if (txt->styles->styles[k].style_flags & (GF_TXT_STYLE_ITALIC | GF_TXT_STYLE_BOLD | GF_TXT_STYLE_UNDERLINED)) {
							new_styles = txt->styles->styles[k].style_flags;
							new_color = txt->styles->styles[k].text_color;
							break;
						}
					}
				}
				if (new_styles != styles) {
					if ((new_styles & GF_TXT_STYLE_BOLD) && !(styles & GF_TXT_STYLE_BOLD)) fprintf(dump, "<b>");
					if ((new_styles & GF_TXT_STYLE_ITALIC) && !(styles & GF_TXT_STYLE_ITALIC)) fprintf(dump, "<i>");
					if ((new_styles & GF_TXT_STYLE_UNDERLINED) && !(styles & GF_TXT_STYLE_UNDERLINED)) fprintf(dump, "<u>");

					if ((styles & GF_TXT_STYLE_UNDERLINED) && !(new_styles & GF_TXT_STYLE_UNDERLINED)) fprintf(dump, "</u>");
					if ((styles & GF_TXT_STYLE_ITALIC) && !(new_styles & GF_TXT_STYLE_ITALIC)) fprintf(dump, "</i>");
					if ((styles & GF_TXT_STYLE_BOLD) && !(new_styles & GF_TXT_STYLE_BOLD)) fprintf(dump, "</b>");

					styles = new_styles;
				}
				if (new_color != color) {
					if (new_color ==txtd->default_style.text_color) {
						fprintf(dump, "</font>");
					} else {
						fprintf(dump, "<font color=\"%s\">", gf_color_get_name(new_color) );
					}
					color = new_color;
				}

				/*not sure if styles must be reseted at line breaks in srt...*/
				is_new_line = GF_FALSE;
				if ((utf16Line[j]=='\n') || (utf16Line[j]=='\r') ) {
					if ((utf16Line[j]=='\r') && (utf16Line[j+1]=='\n')) j++;
					fprintf(dump, "\n");
					is_new_line = GF_TRUE;
				}

				if (!is_new_line) {
					size_t sl;
					char szChar[30];
					s16 swT[2], *swz;
					swT[0] = utf16Line[j];
					swT[1] = 0;
					swz= (s16 *)swT;
					sl = gf_utf8_wcstombs(szChar, 30, (const unsigned short **) &swz);
					if (sl == (size_t)-1) sl=0;
					szChar[(u32) sl]=0;
					fprintf(dump, "%s", szChar);
				}
				char_num++;
			}
			new_styles = 0;
			if (new_styles != styles) {
				if (styles & GF_TXT_STYLE_UNDERLINED) fprintf(dump, "</u>");
				if (styles & GF_TXT_STYLE_ITALIC) fprintf(dump, "</i>");
				if (styles & GF_TXT_STYLE_BOLD) fprintf(dump, "</b>");

//				styles = 0;
			}

			if (color != txtd->default_style.text_color) {
				fprintf(dump, "</font>");
//				color = txtd->default_style.text_color;
			}
			fprintf(dump, "\n");
		}
		gf_isom_sample_del(&s);
		gf_isom_delete_text_sample(txt);
		fprintf(dump, "\n");
		gf_set_progress("SRT Extract", i, count);
	}
	if (count) gf_set_progress("SRT Extract", i, count);
	return GF_OK;
}

static GF_Err gf_isom_dump_svg_track(GF_ISOFile *the_file, u32 track, FILE *dump)
{
	char nhmlFileName[1024];
	FILE *nhmlFile;
	u32 i, count, di, ts, cur_frame;
	u64 start, end;
	GF_BitStream *bs;

	GF_TrackBox *trak = gf_isom_get_track_from_file(the_file, track);
	if (!trak) return GF_BAD_PARAM;
	switch (trak->Media->handler->handlerType) {
	case GF_ISOM_MEDIA_TEXT:
	case GF_ISOM_MEDIA_SUBT:
		break;
	default:
		return GF_BAD_PARAM;
	}

	strcpy(nhmlFileName, the_file->fileName);
	strcat(nhmlFileName, ".nhml");
	nhmlFile = gf_fopen(nhmlFileName, "wt");
	fprintf(nhmlFile, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
	fprintf(nhmlFile, "<NHNTStream streamType=\"3\" objectTypeIndication=\"10\" timeScale=\"%d\" baseMediaFile=\"file.svg\" inRootOD=\"yes\">\n", trak->Media->mediaHeader->timeScale);
	fprintf(nhmlFile, "<NHNTSample isRAP=\"yes\" DTS=\"0\" xmlFrom=\"doc.start\" xmlTo=\"text_1.start\"/>\n");

	ts = trak->Media->mediaHeader->timeScale;
	cur_frame = 0;
	end = 0;

	fprintf(dump, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
	fprintf(dump, "<svg version=\"1.2\" baseProfile=\"tiny\" xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" width=\"%d\" height=\"%d\" fill=\"black\">\n", trak->Header->width >> 16 , trak->Header->height >> 16);
	fprintf(dump, "<g transform=\"translate(%d, %d)\" text-anchor=\"middle\">\n", (trak->Header->width >> 16)/2 , (trak->Header->height >> 16)/2);

	count = gf_isom_get_sample_count(the_file, track);
	for (i=0; i<count; i++) {
		GF_TextSample *txt;
		GF_ISOSample *s = gf_isom_get_sample(the_file, track, i+1, &di);
		if (!s) continue;

		start = s->DTS;
		if (s->dataLength==2) {
			gf_isom_sample_del(&s);
			continue;
		}
		if (i+1<count) {
			GF_ISOSample *next = gf_isom_get_sample_info(the_file, track, i+2, NULL, NULL);
			if (next) {
				end = next->DTS;
				gf_isom_sample_del(&next);
			}
		}

		cur_frame++;
		bs = gf_bs_new(s->data, s->dataLength, GF_BITSTREAM_READ);
		txt = gf_isom_parse_texte_sample(bs);
		gf_bs_del(bs);

		if (!txt->len) continue;

		fprintf(dump, " <text id=\"text_%d\" display=\"none\">%s\n", cur_frame, txt->text);
		fprintf(dump, "  <set attributeName=\"display\" to=\"inline\" begin=\"%g\" end=\"%g\"/>\n", ((s64)start*1.0)/ts, ((s64)end*1.0)/ts);
		fprintf(dump, "  <discard begin=\"%g\"/>\n", ((s64)end*1.0)/ts);
		fprintf(dump, " </text>\n");
		gf_isom_sample_del(&s);
		gf_isom_delete_text_sample(txt);
		fprintf(dump, "\n");
		gf_set_progress("SRT Extract", i, count);

		if (i == count - 2) {
			fprintf(nhmlFile, "<NHNTSample isRAP=\"no\" DTS=\"%f\" xmlFrom=\"text_%d.start\" xmlTo=\"doc.end\"/>\n", ((s64)start*1.0), cur_frame);
		} else {
			fprintf(nhmlFile, "<NHNTSample isRAP=\"no\" DTS=\"%f\" xmlFrom=\"text_%d.start\" xmlTo=\"text_%d.start\"/>\n", ((s64)start*1.0), cur_frame, cur_frame+1);
		}

	}
	fprintf(dump, "</g>\n");
	fprintf(dump, "</svg>\n");

	fprintf(nhmlFile, "</NHNTStream>\n");
	gf_fclose(nhmlFile);

	if (count) gf_set_progress("SRT Extract", i, count);
	return GF_OK;
}

GF_EXPORT
GF_Err gf_isom_text_dump(GF_ISOFile *the_file, u32 track, FILE *dump, GF_TextDumpType dump_type)
{
	switch (dump_type) {
	case GF_TEXTDUMPTYPE_SVG:
		return gf_isom_dump_svg_track(the_file, track, dump);
	case GF_TEXTDUMPTYPE_SRT:
		return gf_isom_dump_srt_track(the_file, track, dump);
	case GF_TEXTDUMPTYPE_TTXT:
	default:
		return gf_isom_dump_ttxt_track(the_file, track, dump);
	}
}


/* ISMA 1.0 Encryption and Authentication V 1.0  dump */
GF_Err sinf_dump(GF_Box *a, FILE * trace)
{
	GF_ProtectionInfoBox *p;
	p = (GF_ProtectionInfoBox *)a;
	DumpBox(a, "ProtectionInfoBox", trace);
	fprintf(trace, ">\n");
	if (p->size)
		gf_box_dump_ex(p->original_format, trace, GF_ISOM_BOX_TYPE_FRMA);
	if (p->size)
		gf_box_dump_ex(p->scheme_type, trace, GF_ISOM_BOX_TYPE_SCHM);
	if (p->size)
		gf_box_dump_ex(p->info, trace, GF_ISOM_BOX_TYPE_SCHI);
	gf_box_dump_done("ProtectionInfoBox", a, trace);
	return GF_OK;
}

GF_Err frma_dump(GF_Box *a, FILE * trace)
{
	GF_OriginalFormatBox *p;
	p = (GF_OriginalFormatBox *)a;
	DumpBox(a, "OriginalFormatBox", trace);
	fprintf(trace, "data_format=\"%s\">\n", gf_4cc_to_str(p->data_format));
	gf_box_dump_done("OriginalFormatBox", a, trace);
	return GF_OK;
}

GF_Err schm_dump(GF_Box *a, FILE * trace)
{
	GF_SchemeTypeBox *p;
	p = (GF_SchemeTypeBox *)a;
	DumpBox(a, "SchemeTypeBox", trace);
	gf_full_box_dump(a, trace);
	fprintf(trace, "scheme_type=\"%s\" scheme_version=\"%d\" ", gf_4cc_to_str(p->scheme_type), p->scheme_version);
	if (p->URI) fprintf(trace, "scheme_uri=\"%s\"", p->URI);
	fprintf(trace, ">\n");

	gf_box_dump_done("SchemeTypeBox", a, trace);
	return GF_OK;
}

GF_Err schi_dump(GF_Box *a, FILE * trace)
{
	GF_SchemeInformationBox *p;
	p = (GF_SchemeInformationBox *)a;
	DumpBox(a, "SchemeInformationBox", trace);
	fprintf(trace, ">\n");
	if (p->ikms) gf_box_dump(p->ikms, trace);
	if (p->isfm) gf_box_dump(p->isfm, trace);
	if (p->okms) gf_box_dump(p->okms, trace);
	if (p->tenc) gf_box_dump(p->tenc, trace);
	if (p->adkm) gf_box_dump(p->adkm, trace);
	gf_box_dump_done("SchemeInformationBox", a, trace);
	return GF_OK;
}

GF_Err iKMS_dump(GF_Box *a, FILE * trace)
{
	GF_ISMAKMSBox *p;
	p = (GF_ISMAKMSBox *)a;
	DumpBox(a, "KMSBox", trace);
	gf_full_box_dump(a, trace);
	fprintf(trace, "kms_URI=\"%s\">\n", p->URI);
	gf_box_dump_done("KMSBox", a, trace);
	return GF_OK;

}

GF_Err iSFM_dump(GF_Box *a, FILE * trace)
{
	GF_ISMASampleFormatBox *p;
	const char *name = (a->type==GF_ISOM_BOX_TYPE_ISFM) ? "ISMASampleFormat" : "OMADRMAUFormatBox";
	p = (GF_ISMASampleFormatBox *)a;
	DumpBox(a, name, trace);
	gf_full_box_dump(a, trace);
	fprintf(trace, "selective_encryption=\"%d\" key_indicator_length=\"%d\" IV_length=\"%d\">\n", p->selective_encryption, p->key_indicator_length, p->IV_length);
	gf_box_dump_done((char *)name, a, trace);
	return GF_OK;
}

static void dump_data(FILE *trace, char *name, char *data, u32 data_size)
{
	u32 i;
	fprintf(trace, "%s=\"0x", name);
	for (i=0; i<data_size; i++) fprintf(trace, "%02X", (unsigned char) data[i]);
	fprintf(trace, "\" ");
}

GF_EXPORT
GF_Err gf_isom_dump_ismacryp_protection(GF_ISOFile *the_file, u32 trackNumber, FILE * trace)
{
	u32 i, count;
	GF_SampleEntryBox *entry;
	GF_Err e;
	GF_TrackBox *trak;

	trak = gf_isom_get_track_from_file(the_file, trackNumber);
	if (!trak) return GF_BAD_PARAM;


	fprintf(trace, "<ISMACrypSampleDescriptions>\n");
	count = gf_isom_get_sample_description_count(the_file, trackNumber);
	for (i=0; i<count; i++) {
		e = Media_GetSampleDesc(trak->Media, i+1, (GF_SampleEntryBox **) &entry, NULL);
		if (e) return e;

		switch (entry->type) {
		case GF_ISOM_BOX_TYPE_ENCA:
		case GF_ISOM_BOX_TYPE_ENCV:
		case GF_ISOM_BOX_TYPE_ENCT:
		case GF_ISOM_BOX_TYPE_ENCS:
			break;
		default:
			continue;
		}
		gf_box_dump(entry, trace);
	}
	fprintf(trace, "</ISMACrypSampleDescriptions>\n");
	return GF_OK;
}

GF_EXPORT
GF_Err gf_isom_dump_ismacryp_sample(GF_ISOFile *the_file, u32 trackNumber, u32 SampleNum, FILE * trace)
{
	GF_ISOSample *samp;
	GF_ISMASample  *isma_samp;
	u32 descIndex;

	samp = gf_isom_get_sample(the_file, trackNumber, SampleNum, &descIndex);
	if (!samp) return GF_BAD_PARAM;

	isma_samp = gf_isom_get_ismacryp_sample(the_file, trackNumber, samp, descIndex);
	if (!isma_samp) {
		gf_isom_sample_del(&samp);
		return GF_NOT_SUPPORTED;
	}

	fprintf(trace, "<ISMACrypSample SampleNumber=\"%d\" DataSize=\"%d\" CompositionTime=\""LLD"\" ", SampleNum, isma_samp->dataLength, LLD_CAST (samp->DTS+samp->CTS_Offset) );
	if (samp->CTS_Offset) fprintf(trace, "DecodingTime=\""LLD"\" ", LLD_CAST samp->DTS);
	if (gf_isom_has_sync_points(the_file, trackNumber)) fprintf(trace, "RandomAccessPoint=\"%s\" ", samp->IsRAP ? "Yes" : "No");
	fprintf(trace, "IsEncrypted=\"%s\" ", (isma_samp->flags & GF_ISOM_ISMA_IS_ENCRYPTED) ? "Yes" : "No");
	if (isma_samp->flags & GF_ISOM_ISMA_IS_ENCRYPTED) {
		fprintf(trace, "IV=\""LLD"\" ", LLD_CAST isma_samp->IV);
		if (isma_samp->key_indicator) dump_data(trace, "KeyIndicator", (char*)isma_samp->key_indicator, isma_samp->KI_length);
	}
	fprintf(trace, "/>\n");

	gf_isom_sample_del(&samp);
	gf_isom_ismacryp_delete_sample(isma_samp);
	return GF_OK;
}

/* end of ISMA 1.0 Encryption and Authentication V 1.0 */


/* Apple extensions */

static GF_Err apple_tag_dump(GF_Box *a, FILE * trace)
{
	GF_BitStream *bs;
	u32 val;
	Bool no_dump = GF_FALSE;
	char *name = "UnknownBox";
	GF_ListItemBox *itune = (GF_ListItemBox *)a;
	switch (itune->type) {
	case GF_ISOM_BOX_TYPE_0xA9NAM:
		name = "NameBox";
		break;
	case GF_ISOM_BOX_TYPE_0xA9CMT:
		name = "CommentBox";
		break;
	case GF_ISOM_BOX_TYPE_0xA9DAY:
		name = "CreatedBox";
		break;
	case GF_ISOM_BOX_TYPE_0xA9ART:
		name = "ArtistBox";
		break;
	case GF_ISOM_BOX_TYPE_0xA9TRK:
		name = "TrackBox";
		break;
	case GF_ISOM_BOX_TYPE_0xA9ALB:
		name = "AlbumBox";
		break;
	case GF_ISOM_BOX_TYPE_0xA9COM:
		name = "CompositorBox";
		break;
	case GF_ISOM_BOX_TYPE_0xA9WRT:
		name = "WriterBox";
		break;
	case GF_ISOM_BOX_TYPE_0xA9TOO:
		name = "ToolBox";
		break;
	case GF_ISOM_BOX_TYPE_0xA9CPY:
		name = "CopyrightBox";
		break;
	case GF_ISOM_BOX_TYPE_0xA9DES:
		name = "DescriptionBox";
		break;
	case GF_ISOM_BOX_TYPE_0xA9GEN:
	case GF_ISOM_BOX_TYPE_GNRE:
		name = "GenreBox";
		break;
	case GF_ISOM_BOX_TYPE_aART:
		name = "AlbumArtistBox";
		break;
	case GF_ISOM_BOX_TYPE_PGAP:
		name = "GapelessBox";
		break;
	case GF_ISOM_BOX_TYPE_DISK:
		name = "DiskBox";
		break;
	case GF_ISOM_BOX_TYPE_TRKN:
		name = "TrackNumberBox";
		break;
	case GF_ISOM_BOX_TYPE_TMPO:
		name = "TempoBox";
		break;
	case GF_ISOM_BOX_TYPE_CPIL:
		name = "CompilationBox";
		break;
	case GF_ISOM_BOX_TYPE_COVR:
		name = "CoverArtBox";
		no_dump = GF_TRUE;
		break;
	case GF_ISOM_BOX_TYPE_iTunesSpecificInfo:
		name = "iTunesSpecificBox";
		no_dump = GF_TRUE;
		break;
	case GF_ISOM_BOX_TYPE_0xA9GRP:
		name = "GroupBox";
		break;
	case GF_ISOM_ITUNE_ENCODER:
		name = "EncoderBox";
		break;
	}
	DumpBox(a, name, trace);
	if (!no_dump) {
		switch (itune->type) {
		case GF_ISOM_BOX_TYPE_DISK:
		case GF_ISOM_BOX_TYPE_TRKN:
			bs = gf_bs_new(itune->data->data, itune->data->dataSize, GF_BITSTREAM_READ);
			gf_bs_read_int(bs, 16);
			val = gf_bs_read_int(bs, 16);
			if (itune->type==GF_ISOM_BOX_TYPE_DISK) {
				fprintf(trace, " DiskNumber=\"%d\" NbDisks=\"%d\" ", val, gf_bs_read_int(bs, 16) );
			} else {
				fprintf(trace, " TrackNumber=\"%d\" NbTracks=\"%d\" ", val, gf_bs_read_int(bs, 16) );
			}
			gf_bs_del(bs);
			break;
		case GF_ISOM_BOX_TYPE_TMPO:
			bs = gf_bs_new(itune->data->data, itune->data->dataSize, GF_BITSTREAM_READ);
			fprintf(trace, " BPM=\"%d\" ", gf_bs_read_int(bs, 16) );
			gf_bs_del(bs);
			break;
		case GF_ISOM_BOX_TYPE_CPIL:
			fprintf(trace, " IsCompilation=\"%s\" ", (itune->data && itune->data->data && itune->data->data[0]) ? "yes" : "no");
			break;
		case GF_ISOM_BOX_TYPE_PGAP:
			fprintf(trace, " IsGapeless=\"%s\" ", (itune->data && itune->data->data && itune->data->data[0]) ? "yes" : "no");
			break;
		default:
			if (strcmp(name, "UnknownBox") && itune->data && itune->data->data) {
				fprintf(trace, " value=\"");
				if (itune->data && itune->data->data[0]) {
					DumpDataString(trace, itune->data->data, itune->data->dataSize);
				} else {
					DumpData(trace, itune->data->data, itune->data->dataSize);
				}
				fprintf(trace, "\" ");
			}
			break;
		}
	}
	fprintf(trace, ">\n");
	if (strcmp(name, "UnknownBox") && itune->data)
		gf_full_box_dump((GF_Box *)itune->data, trace);
	gf_box_dump_done(name, a, trace);
	return GF_OK;
}

#ifndef GPAC_DISABLE_ISOM_ADOBE

GF_Err abst_dump(GF_Box *a, FILE * trace)
{
	u32 i;
	GF_AdobeBootstrapInfoBox *p = (GF_AdobeBootstrapInfoBox*)a;
	DumpBox(a, "AdobeBootstrapBox", trace);
	gf_full_box_dump(a, trace);
	fprintf(trace, "BootstrapinfoVersion=\"%u\" Profile=\"%u\" Live=\"%u\" Update=\"%u\" TimeScale=\"%u\" CurrentMediaTime=\""LLU"\" SmpteTimeCodeOffset=\""LLU"\" ",
	        p->bootstrapinfo_version, p->profile, p->live, p->update, p->time_scale, p->current_media_time, p->smpte_time_code_offset);
	if (p->movie_identifier)
		fprintf(trace, "MovieIdentifier=\"%s\" ", p->movie_identifier);
	if (p->drm_data)
		fprintf(trace, "DrmData=\"%s\" ", p->drm_data);
	if (p->meta_data)
		fprintf(trace, "MetaData=\"%s\" ", p->meta_data);
	fprintf(trace, ">\n");

	for (i=0; i<p->server_entry_count; i++) {
		char *str = (char*)gf_list_get(p->server_entry_table, i);
		fprintf(trace, "<ServerEntry>%s</ServerEntry>\n", str);
	}

	for (i=0; i<p->quality_entry_count; i++) {
		char *str = (char*)gf_list_get(p->quality_entry_table, i);
		fprintf(trace, "<QualityEntry>%s</QualityEntry>\n", str);
	}

	for (i=0; i<p->segment_run_table_count; i++)
		gf_box_dump(gf_list_get(p->segment_run_table_entries, i), trace);

	for (i=0; i<p->fragment_run_table_count; i++)
		gf_box_dump(gf_list_get(p->fragment_run_table_entries, i), trace);

	gf_box_dump_done("AdobeBootstrapBox", a, trace);
	return GF_OK;
}

GF_Err afra_dump(GF_Box *a, FILE * trace)
{
	u32 i;
	GF_AdobeFragRandomAccessBox *p = (GF_AdobeFragRandomAccessBox*)a;
	DumpBox(a, "AdobeFragmentRandomAccessBox", trace);
	gf_full_box_dump(a, trace);
	fprintf(trace, "LongIDs=\"%u\" LongOffsets=\"%u\" TimeScale=\"%u\">\n", p->long_ids, p->long_offsets, p->time_scale);

	for (i=0; i<p->entry_count; i++) {
		GF_AfraEntry *ae = (GF_AfraEntry *)gf_list_get(p->local_access_entries, i);
		fprintf(trace, "<LocalAccessEntry Time=\""LLU"\" Offset=\""LLU"\"/>\n", ae->time, ae->offset);
	}

	for (i=0; i<p->global_entry_count; i++) {
		GF_GlobalAfraEntry *gae = (GF_GlobalAfraEntry *)gf_list_get(p->global_access_entries, i);
		fprintf(trace, "<GlobalAccessEntry Time=\""LLU"\" Segment=\"%u\" Fragment=\"%u\" AfraOffset=\""LLU"\" OffsetFromAfra=\""LLU"\"/>\n",
		        gae->time, gae->segment, gae->fragment, gae->afra_offset, gae->offset_from_afra);
	}

	gf_box_dump_done("AdobeFragmentRandomAccessBox", a, trace);
	return GF_OK;
}

GF_Err afrt_dump(GF_Box *a, FILE * trace)
{
	u32 i;
	GF_AdobeFragmentRunTableBox *p = (GF_AdobeFragmentRunTableBox*)a;
	DumpBox(a, "AdobeFragmentRunTableBox", trace);
	gf_full_box_dump(a, trace);
	fprintf(trace, "TimeScale=\"%u\">\n", p->timescale);

	for (i=0; i<p->quality_entry_count; i++) {
		char *str = (char*)gf_list_get(p->quality_segment_url_modifiers, i);
		fprintf(trace, "<QualityEntry>%s</QualityEntry>\n", str);
	}

	for (i=0; i<p->fragment_run_entry_count; i++) {
		GF_AdobeFragmentRunEntry *fre = (GF_AdobeFragmentRunEntry *)gf_list_get(p->fragment_run_entry_table, i);
		fprintf(trace, "<FragmentRunEntry FirstFragment=\"%u\" FirstFragmentTimestamp=\""LLU"\" FirstFragmentDuration=\"%u\"", fre->first_fragment, fre->first_fragment_timestamp, fre->fragment_duration);
		if (!fre->fragment_duration)
			fprintf(trace, " DiscontinuityIndicator=\"%u\"", fre->discontinuity_indicator);
		fprintf(trace, "/>\n");
	}

	gf_box_dump_done("AdobeFragmentRunTableBox", a, trace);
	return GF_OK;
}

GF_Err asrt_dump(GF_Box *a, FILE * trace)
{
	u32 i;
	GF_AdobeSegmentRunTableBox *p = (GF_AdobeSegmentRunTableBox*)a;
	DumpBox(a, "AdobeSegmentRunTableBox", trace);
	gf_full_box_dump(a, trace);
	fprintf(trace, ">\n");

	for (i=0; i<p->quality_entry_count; i++) {
		char *str = (char*)gf_list_get(p->quality_segment_url_modifiers, i);
		fprintf(trace, "<QualityEntry>%s</QualityEntry>\n", str);
	}

	for (i=0; i<p->segment_run_entry_count; i++) {
		GF_AdobeSegmentRunEntry *sre = (GF_AdobeSegmentRunEntry *)gf_list_get(p->segment_run_entry_table, i);
		fprintf(trace, "<SegmentRunEntry FirstSegment=\"%u\" FragmentsPerSegment=\"%u\"/>\n", sre->first_segment, sre->fragment_per_segment);
	}

	gf_box_dump_done("AdobeSegmentRunTableBox", a, trace);
	return GF_OK;
}

#endif /*GPAC_DISABLE_ISOM_ADOBE*/

GF_Err ilst_dump(GF_Box *a, FILE * trace)
{
	u32 i;
	GF_Box *tag;
	GF_Err e;
	GF_ItemListBox *ptr;
	ptr = (GF_ItemListBox *)a;
	DumpBox(a, "ItemListBox", trace);
	fprintf(trace, ">\n");

	i=0;
	while ( (tag = (GF_Box*)gf_list_enum(ptr->other_boxes, &i))) {
		e = apple_tag_dump(tag, trace);
		if(e) return e;
	}
	gf_box_dump_done("ItemListBox", NULL, trace);
	return GF_OK;
}

GF_Err ListEntry_dump(GF_Box *a, FILE * trace)
{
	DumpBox(a, "ListEntry", trace);
	gf_box_dump(a, trace);
	fprintf(trace, ">\n");
	gf_box_dump_done("ListEntry", a, trace);
	return GF_OK;
}

GF_Err data_dump(GF_Box *a, FILE * trace)
{
	DumpBox(a, "data", trace);
	gf_full_box_dump(a, trace);
	fprintf(trace, ">\n");
	gf_box_dump_done("data", a, trace);
	return GF_OK;
}

GF_Err ohdr_dump(GF_Box *a, FILE * trace)
{
	GF_OMADRMCommonHeaderBox *ptr = (GF_OMADRMCommonHeaderBox *)a;
	DumpBox(a, "OMADRMCommonHeaderBox", trace);
	gf_full_box_dump(a, trace);
	fprintf(trace, "EncryptionMethod=\"%d\" PaddingScheme=\"%d\" PlaintextLength=\""LLD"\" ",
	        ptr->EncryptionMethod, ptr->PaddingScheme, ptr->PlaintextLength);
	if (ptr->RightsIssuerURL) fprintf(trace, "RightsIssuerURL=\"%s\" ", ptr->RightsIssuerURL);
	if (ptr->ContentID) fprintf(trace, "ContentID=\"%s\" ", ptr->ContentID);
	if (ptr->TextualHeaders) {
		u32 i, offset;
		char *start = ptr->TextualHeaders;
		fprintf(trace, "TextualHeaders=\"");
		i=offset=0;
		while (i<ptr->TextualHeadersLen) {
			if (start[i]==0) {
				fprintf(trace, "%s ", start+offset);
				offset=i+1;
			}
			i++;
		}
		fprintf(trace, "%s\"  ", start+offset);
	}

	fprintf(trace, ">\n");
	gf_box_dump_done("OMADRMCommonHeaderBox", a, trace);
	return GF_OK;
}
GF_Err grpi_dump(GF_Box *a, FILE * trace)
{
	GF_OMADRMGroupIDBox *ptr = (GF_OMADRMGroupIDBox *)a;
	DumpBox(a, "OMADRMGroupIDBox", trace);
	gf_full_box_dump((GF_Box *)a, trace);
	fprintf(trace, "GroupID=\"%s\" EncryptionMethod=\"%d\" GroupKey=\" ", ptr->GroupID, ptr->GKEncryptionMethod);
	if (ptr->GroupKey)
		DumpData(trace, ptr->GroupKey, ptr->GKLength);
	fprintf(trace, "\">\n");
	gf_box_dump_done("OMADRMGroupIDBox", a, trace);
	return GF_OK;
}
GF_Err mdri_dump(GF_Box *a, FILE * trace)
{
	//GF_OMADRMMutableInformationBox *ptr = (GF_OMADRMMutableInformationBox*)a;
	DumpBox(a, "OMADRMMutableInformationBox", trace);
	fprintf(trace, ">\n");
	gf_box_dump_done("OMADRMMutableInformationBox", a, trace);
	return GF_OK;
}
GF_Err odtt_dump(GF_Box *a, FILE * trace)
{
	GF_OMADRMTransactionTrackingBox *ptr = (GF_OMADRMTransactionTrackingBox *)a;
	DumpBox(a, "OMADRMTransactionTrackingBox", trace);
	gf_full_box_dump((GF_Box *)a, trace);
	fprintf(trace, "TransactionID=\"");
	DumpData(trace, ptr->TransactionID, 16);
	fprintf(trace, "\">\n");
	gf_box_dump_done("OMADRMTransactionTrackingBox", a, trace);
	return GF_OK;
}
GF_Err odrb_dump(GF_Box *a, FILE * trace)
{
	GF_OMADRMRightsObjectBox*ptr = (GF_OMADRMRightsObjectBox*)a;
	DumpBox(a, "OMADRMRightsObjectBox", trace);
	gf_full_box_dump((GF_Box *)a, trace);
	fprintf(trace, "OMARightsObject=\"");
	DumpData(trace, ptr->oma_ro, ptr->oma_ro_size);
	fprintf(trace, "\">\n");
	gf_box_dump_done("OMADRMRightsObjectBox", a, trace);
	return GF_OK;
}
GF_Err odkm_dump(GF_Box *a, FILE * trace)
{
	GF_OMADRMKMSBox *ptr = (GF_OMADRMKMSBox*)a;
	DumpBox(a, "OMADRMKMSBox", trace);
	fprintf(trace, ">\n");
	gf_full_box_dump((GF_Box *)a, trace);
	if (ptr->hdr) gf_box_dump((GF_Box *)ptr->hdr, trace);
	if (ptr->fmt) gf_box_dump((GF_Box *)ptr->fmt, trace);
	gf_box_dump_done("OMADRMKMSBox", a, trace);
	return GF_OK;
}


GF_Err pasp_dump(GF_Box *a, FILE * trace)
{
	GF_PixelAspectRatioBox *ptr = (GF_PixelAspectRatioBox*)a;
	DumpBox(a, "PixelAspectRatioBox", trace);
	fprintf(trace, "hSpacing=\"%d\" vSpacing=\"%d\" >\n", ptr->hSpacing, ptr->vSpacing);
	gf_box_dump_done("PixelAspectRatioBox", a, trace);
	return GF_OK;
}


GF_Err tsel_dump(GF_Box *a, FILE * trace)
{
	u32 i;
	GF_TrackSelectionBox *ptr = (GF_TrackSelectionBox *)a;
	DumpBox(a, "TrackSelectionBox", trace);
	gf_full_box_dump((GF_Box *)a, trace);
	fprintf(trace, "switchGroup=\"%d\" criteria=\"", ptr->switchGroup);
	for (i=0; i<ptr->attributeListCount; i++) {
		if (i) fprintf(trace, ";");
		fprintf(trace, "%s", gf_4cc_to_str(ptr->attributeList[i]));
	}
	fprintf(trace, "\">\n");
	gf_box_dump_done("TrackSelectionBox", a, trace);
	return GF_OK;
}

GF_Err metx_dump(GF_Box *a, FILE * trace)
{
	GF_MetaDataSampleEntryBox *ptr = (GF_MetaDataSampleEntryBox*)a;
	const char *name;
	switch (ptr->type) {
	case GF_ISOM_BOX_TYPE_METX:
		name = "XMLMetaDataSampleEntryBox";
		break;
	case GF_ISOM_BOX_TYPE_METT:
		name = "TextMetaDataSampleEntryBox";
		break;
	case GF_ISOM_BOX_TYPE_SBTT:
		name = "SubtitleSampleEntryBox";
		break;
	case GF_ISOM_BOX_TYPE_STXT:
		name = "SimpleTextSampleEntryBox";
		break;
	case GF_ISOM_BOX_TYPE_STPP:
		name = "XMLSubtitleSampleEntryBox";
		break;
	default:
		name = "UnknownTextSampleEntryBox";
		break;
	}
	DumpBox(a, name, trace);

	if (ptr->type==GF_ISOM_BOX_TYPE_METX) {
		fprintf(trace, "namespace=\"%s\" ", ptr->xml_namespace);
		if (ptr->xml_schema_loc) fprintf(trace, "schema_location=\"%s\" ", ptr->xml_schema_loc);
		if (ptr->content_encoding) fprintf(trace, "content_encoding=\"%s\" ", ptr->content_encoding);

	} else if (ptr->type==GF_ISOM_BOX_TYPE_STPP) {
		fprintf(trace, "namespace=\"%s\" ", ptr->xml_namespace);
		if (ptr->xml_schema_loc) fprintf(trace, "schema_location=\"%s\" ", ptr->xml_schema_loc);
		if (ptr->mime_type) fprintf(trace, "auxiliary_mime_types=\"%s\" ", ptr->mime_type);
	}
	//mett, sbtt, stxt
	else {
		fprintf(trace, "mime_type=\"%s\" ", ptr->mime_type);
		if (ptr->content_encoding) fprintf(trace, "content_encoding=\"%s\" ", ptr->content_encoding);
	}
	fprintf(trace, ">\n");

	if ((ptr->type!=GF_ISOM_BOX_TYPE_METX) && (ptr->type!=GF_ISOM_BOX_TYPE_STPP) ) {
		if (ptr->config) gf_box_dump(ptr->config, trace);
	}
	gf_box_array_dump(ptr->protections, trace);

	gf_box_dump_done((char *)name, a, trace);
	return GF_OK;
}

GF_Err txtc_dump(GF_Box *a, FILE * trace)
{
	GF_TextConfigBox *ptr = (GF_TextConfigBox*)a;
	const char *name = "TextConfigBox";

	DumpBox(a, name, trace);
	fprintf(trace, ">\n");

	if (ptr->config) fprintf(trace, "%s", ptr->config);

	gf_box_dump_done((char *)name, a, trace);
	return GF_OK;
}

GF_Err dims_dump(GF_Box *a, FILE * trace)
{
	GF_DIMSSampleEntryBox *p = (GF_DIMSSampleEntryBox*)a;
	DumpBox(a, "DIMSSampleEntryBox", trace);
	fprintf(trace, "dataReferenceIndex=\"%d\">\n", p->dataReferenceIndex);
	if (p->config) gf_box_dump(p->config, trace);
	if (p->scripts) gf_box_dump(p->scripts, trace);
	gf_box_array_dump(p->protections, trace);
	gf_box_dump_done("DIMSSampleEntryBox", a, trace);
	return GF_OK;
}

GF_Err diST_dump(GF_Box *a, FILE * trace)
{
	GF_DIMSScriptTypesBox *p = (GF_DIMSScriptTypesBox*)a;
	DumpBox(a, "DIMSScriptTypesBox", trace);
	fprintf(trace, "types=\"%s\">\n", p->content_script_types);
	gf_box_dump_done("DIMSScriptTypesBox", a, trace);
	return GF_OK;
}

GF_Err dimC_dump(GF_Box *a, FILE * trace)
{
	GF_DIMSSceneConfigBox *p = (GF_DIMSSceneConfigBox *)a;
	DumpBox(a, "DIMSSceneConfigBox", trace);
	fprintf(trace, "profile=\"%d\" level=\"%d\" pathComponents=\"%d\" useFullRequestHosts=\"%d\" streamType=\"%d\" containsRedundant=\"%d\" textEncoding=\"%s\" contentEncoding=\"%s\" >\n",
	        p->profile, p->level, p->pathComponents, p->fullRequestHost, p->streamType, p->containsRedundant, p->textEncoding, p->contentEncoding);
	gf_box_dump_done("DIMSSceneConfigBox", a, trace);
	return GF_OK;
}


GF_Err dac3_dump(GF_Box *a, FILE * trace)
{
	GF_AC3ConfigBox *p = (GF_AC3ConfigBox *)a;

	if (p->cfg.is_ec3) {
		u32 i;
		a->type = GF_ISOM_BOX_TYPE_DEC3;
		DumpBox(a, "EC3SpecificBox", trace);
		a->type = GF_ISOM_BOX_TYPE_DAC3;
		fprintf(trace, "nb_streams=\"%d\" data_rate=\"%d\">\n", p->cfg.nb_streams, p->cfg.brcode);
		for (i=0; i<p->cfg.nb_streams; i++) {
			fprintf(trace, "<EC3StreamConfig fscod=\"%d\" bsid=\"%d\" bsmod=\"%d\" acmod=\"%d\" lfon=\"%d\" num_sub_dep=\"%d\" chan_loc=\"%d\"/>\n",
			        p->cfg.streams[i].fscod, p->cfg.streams[i].bsid, p->cfg.streams[i].bsmod, p->cfg.streams[i].acmod, p->cfg.streams[i].lfon, p->cfg.streams[i].nb_dep_sub, p->cfg.streams[i].chan_loc);
		}
		gf_box_dump_done("EC3SpecificBox", a, trace);
	} else {
		DumpBox(a, "AC3SpecificBox", trace);
		fprintf(trace, "fscod=\"%d\" bsid=\"%d\" bsmod=\"%d\" acmod=\"%d\" lfon=\"%d\" bit_rate_code=\"%d\">\n",
		        p->cfg.streams[0].fscod, p->cfg.streams[0].bsid, p->cfg.streams[0].bsmod, p->cfg.streams[0].acmod, p->cfg.streams[0].lfon, p->cfg.brcode);
		gf_box_dump_done("AC3SpecificBox", a, trace);
	}
	return GF_OK;
}

GF_Err ac3_dump(GF_Box *a, FILE * trace)
{
	GF_AC3SampleEntryBox *p = (GF_AC3SampleEntryBox *)a;
	const char *name;
	if (p->is_ec3)
		name = "EC3SampleEntryBox";
	else
		name = "AC3SampleEntryBox";
	if (p->is_ec3)
		a->type = GF_ISOM_BOX_TYPE_EC3;
	DumpBox(a, name, trace);
	if (p->is_ec3)
		a->type = GF_ISOM_BOX_TYPE_AC3;
	base_audio_entry_dump((GF_AudioSampleEntryBox *)p, trace);
	fprintf(trace, ">\n");
	if (p->size)
		gf_box_dump_ex(p->info, trace, p->is_ec3 ? GF_ISOM_BOX_TYPE_DEC3 : GF_ISOM_BOX_TYPE_DAC3);
	gf_box_dump_done((char *)name, a, trace);
	return GF_OK;
}

GF_Err lsrc_dump(GF_Box *a, FILE * trace)
{
	GF_LASERConfigurationBox *p = (GF_LASERConfigurationBox *)a;
	DumpBox(a, "LASeRConfigurationBox", trace);
	dump_data(trace, "LASeRHeader", p->hdr, p->hdr_size);
	fprintf(trace, ">");
	gf_box_dump_done("LASeRConfigurationBox", a, trace);
	return GF_OK;
}

GF_Err lsr1_dump(GF_Box *a, FILE * trace)
{
	GF_LASeRSampleEntryBox *p = (GF_LASeRSampleEntryBox*)a;
	DumpBox(a, "LASeRSampleEntryBox", trace);
	fprintf(trace, "DataReferenceIndex=\"%d\">\n", p->dataReferenceIndex);
	if (p->lsr_config) gf_box_dump(p->lsr_config, trace);
	if (p->descr) gf_box_dump(p->descr, trace);
	gf_box_dump_done("LASeRSampleEntryBox", a, trace);
	return GF_OK;
}


GF_Err sidx_dump(GF_Box *a, FILE * trace)
{
	u32 i;
	GF_SegmentIndexBox *p = (GF_SegmentIndexBox *)a;
	DumpBox(a, "SegmentIndexBox", trace);
	fprintf(trace, "reference_ID=\"%d\" timescale=\"%d\" earliest_presentation_time=\""LLD"\" first_offset=\""LLD"\" ", p->reference_ID, p->timescale, p->earliest_presentation_time, p->first_offset);
	gf_full_box_dump(a, trace);
	fprintf(trace, ">\n");
	for (i=0; i<p->nb_refs; i++) {
		fprintf(trace, "<Reference type=\"%d\" size=\"%d\" duration=\"%d\" startsWithSAP=\"%d\" SAP_type=\"%d\" SAPDeltaTime=\"%d\"/>\n", p->refs[i].reference_type, p->refs[i].reference_size, p->refs[i].subsegment_duration, p->refs[i].starts_with_SAP, p->refs[i].SAP_type, p->refs[i].SAP_delta_time);
	}
	if (!p->size) {
		fprintf(trace, "<Reference type=\"\" size=\"\" duration=\"\" startsWithSAP=\"\" SAP_type=\"\" SAPDeltaTime=\"\"/>\n");
	}
	gf_box_dump_done("SegmentIndexBox", a, trace);
	return GF_OK;
}

GF_Err ssix_dump(GF_Box *a, FILE * trace)
{
	u32 i, j;
	GF_SubsegmentIndexBox *p = (GF_SubsegmentIndexBox *)a;
	DumpBox(a, "SubsegmentIndexBox", trace);
	gf_full_box_dump(a, trace);
	fprintf(trace, "subsegment_count=\"%d\" >\n", p->subsegment_count);
	for (i = 0; i < p->subsegment_count; i++) {
		fprintf(trace, "<Subsegment range_count=\"%d\">\n", p->subsegments[i].range_count);
		for (j = 0; j < p->subsegments[i].range_count; j++) {
			fprintf(trace, "<Range level=\"%d\" range_size=\"%d\"/>\n", p->subsegments[i].levels[j], p->subsegments[i].range_sizes[j]);
		}
		fprintf(trace, "</Subsegment>\n");
	}
	if (!p->size) {
		fprintf(trace, "<Subsegment range_count=\"\">\n");
		fprintf(trace, "<Range level=\"\" range_size=\"\"/>\n");
		fprintf(trace, "</Subsegment>\n");
	}
	gf_box_dump_done("SubsegmentIndexBox", a, trace);
	return GF_OK;
}


GF_Err leva_dump(GF_Box *a, FILE * trace)
{
	u32 i;
	GF_LevelAssignmentBox *p = (GF_LevelAssignmentBox *)a;
	DumpBox(a, "LevelAssignmentBox", trace);
	gf_full_box_dump(a, trace);
	fprintf(trace, "level_count=\"%d\" >\n", p->level_count);
	for (i = 0; i < p->level_count; i++) {
		fprintf(trace, "<Assignement track_id=\"%d\" padding_flag=\"%d\" assignement_type=\"%d\" grouping_type=\"%d\" grouping_type_parameter=\"%d\" sub_track_id=\"%d\" />\n", p->levels[i].track_id, p->levels[i].padding_flag, p->levels[i].type, p->levels[i].grouping_type, p->levels[i].grouping_type_parameter, p->levels[i].sub_track_id);
	}
	if (!p->size) {
		fprintf(trace, "<Assignement track_id=\"\" padding_flag=\"\" assignement_type=\"\" grouping_type=\"\" grouping_type_parameter=\"\" sub_track_id=\"\" />\n");
	}
	gf_box_dump_done("LevelAssignmentBox", a, trace);
	return GF_OK;
}

GF_Err strk_dump(GF_Box *a, FILE * trace)
{
	GF_SubTrackBox *p = (GF_SubTrackBox *)a;
	DumpBox(a, "SubTrackBox", trace);
	fprintf(trace, ">\n");
	if (p->info) {
		gf_box_dump(p->info, trace);
	}
	gf_box_dump_done("SubTrackBox", a, trace);
	return GF_OK;
}

GF_Err stri_dump(GF_Box *a, FILE * trace)
{
	u32 i;
	GF_SubTrackInformationBox *p = (GF_SubTrackInformationBox *)a;
	DumpBox(a, "SubTrackInformationBox", trace);
	gf_full_box_dump(a, trace);
	fprintf(trace, "switch_group=\"%d\" alternate_group=\"%d\" sub_track_id=\"%d\" ", p->switch_group, p->alternate_group, p->sub_track_id);
	fprintf(trace, "attribute_list=\"");
	for (i = 0; i < p->attribute_count; i++) {
		fprintf(trace, "%s ", gf_4cc_to_str(p->attribute_list[i]));
	}
	fprintf(trace, "\" ");
	fprintf(trace, "/>\n");
	return GF_OK;
}

GF_Err pcrb_dump(GF_Box *a, FILE * trace)
{
	u32 i;
	GF_PcrInfoBox *p = (GF_PcrInfoBox *)a;
	DumpBox(a, "MPEG2TSPCRInfoBox", trace);
	fprintf(trace, "subsegment_count=\"%d\">\n", p->subsegment_count);

	for (i=0; i<p->subsegment_count; i++) {
		fprintf(trace, "<PCRInfo PCR=\""LLU"\" />\n", p->pcr_values[i]);
	}
	if (!p->size) {
		fprintf(trace, "<PCRInfo PCR=\"\" />\n");
	}
	gf_box_dump_done("MPEG2TSPCRInfoBox", a, trace);
	return GF_OK;
}

GF_Err subs_dump(GF_Box *a, FILE * trace)
{
	u32 entry_count, i, j;
	u16 subsample_count;
	GF_SubSampleInfoEntry *pSamp;
	GF_SubSampleEntry *pSubSamp;
	GF_SubSampleInformationBox *ptr = (GF_SubSampleInformationBox *) a;

	if (!a) return GF_BAD_PARAM;

	entry_count = gf_list_count(ptr->Samples);
	DumpBox(a, "SubSampleInformationBox", trace);
	fprintf(trace, "EntryCount=\"%d\">\n", entry_count);

	for (i=0; i<entry_count; i++) {
		pSamp = (GF_SubSampleInfoEntry*) gf_list_get(ptr->Samples, i);

		subsample_count = gf_list_count(pSamp->SubSamples);

		fprintf(trace, "<SampleEntry SampleDelta=\"%d\" SubSampleCount=\"%d\">\n", pSamp->sample_delta, subsample_count);

		for (j=0; j<subsample_count; j++) {
			pSubSamp = (GF_SubSampleEntry*) gf_list_get(pSamp->SubSamples, j);
			fprintf(trace, "<SubSample Size=\"%u\" Priority=\"%u\" Discardable=\"%d\" Reserved=\"%08X\"/>\n", pSubSamp->subsample_size, pSubSamp->subsample_priority, pSubSamp->discardable, pSubSamp->reserved);
		}
		fprintf(trace, "</SampleEntry>\n");
	}
	if (!ptr->size) {
		fprintf(trace, "<SampleEntry SampleDelta=\"\" SubSampleCount=\"\">\n");
		fprintf(trace, "<SubSample Size=\"\" Priority=\"\" Discardable=\"\" Reserved=\"\"/>\n");
		fprintf(trace, "</SampleEntry>\n");
	}

	gf_box_dump_done("SubSampleInformationBox", a, trace);
	return GF_OK;
}

#ifndef GPAC_DISABLE_ISOM_FRAGMENTS
GF_Err tfdt_dump(GF_Box *a, FILE * trace)
{
	GF_TFBaseMediaDecodeTimeBox *ptr = (GF_TFBaseMediaDecodeTimeBox*) a;
	if (!a) return GF_BAD_PARAM;
	DumpBox(a, "TrackFragmentBaseMediaDecodeTimeBox", trace);
	gf_full_box_dump(a, trace);
	fprintf(trace, "baseMediaDecodeTime=\""LLD"\">\n", ptr->baseMediaDecodeTime);
	gf_box_dump_done("TrackFragmentBaseMediaDecodeTimeBox", a, trace);
	return GF_OK;
}
#endif /*GPAC_DISABLE_ISOM_FRAGMENTS*/

GF_Err rvcc_dump(GF_Box *a, FILE * trace)
{
	GF_RVCConfigurationBox *ptr = (GF_RVCConfigurationBox*) a;
	if (!a) return GF_BAD_PARAM;

	DumpBox(a, "RVCConfigurationBox", trace);
	fprintf(trace, "predefined=\"%d\"", ptr->predefined_rvc_config);
	if (! ptr->predefined_rvc_config) fprintf(trace, " rvc_meta_idx=\"%d\"", ptr->rvc_meta_idx);
	fprintf(trace, ">\n");
	gf_box_dump_done("RVCConfigurationBox", a, trace);
	return GF_OK;
}

GF_Err sbgp_dump(GF_Box *a, FILE * trace)
{
	u32 i;
	GF_SampleGroupBox *ptr = (GF_SampleGroupBox*) a;
	if (!a) return GF_BAD_PARAM;

	DumpBox(a, "SampleGroupBox", trace);
	gf_full_box_dump((GF_Box *)a, trace);
	fprintf(trace, "grouping_type=\"%s\"", gf_4cc_to_str(ptr->grouping_type) );
	if (ptr->version==1) {
		if (isalnum(ptr->grouping_type_parameter&0xFF)) {
			fprintf(trace, " grouping_type_parameter=\"%s\"", gf_4cc_to_str(ptr->grouping_type_parameter) );
		} else {
			fprintf(trace, " grouping_type_parameter=\"%d\"", ptr->grouping_type_parameter);
		}
	}
	fprintf(trace, ">\n");
	for (i=0; i<ptr->entry_count; i++) {
		fprintf(trace, "<SampleGroupBoxEntry sample_count=\"%d\" group_description_index=\"%d\"/>\n", ptr->sample_entries[i].sample_count, ptr->sample_entries[i].group_description_index );
	}
	if (!ptr->size) {
		fprintf(trace, "<SampleGroupBoxEntry sample_count=\"\" group_description_index=\"\"/>\n");
	}
	gf_box_dump_done("SampleGroupBox", a, trace);
	return GF_OK;
}

static void oinf_dump(GF_OperatingPointsInformation *ptr, FILE * trace)
{
	u32 i, count;

	if (!ptr) {
		fprintf(trace, "<OperatingPointsInformation scalability_mask=\"Multiview|Spatial scalability|Auxilary|unknown\" num_profile_tier_level=\"\" num_operating_points=\"\" dependency_layers=\"\">\n");

		fprintf(trace, " <ProfileTierLevel general_profile_space=\"\" general_tier_flag=\"\" general_profile_idc=\"\" general_profile_compatibility_flags=\"\" general_constraint_indicator_flags=\"\" />\n");

		fprintf(trace, "<OperatingPoint output_layer_set_idx=\"\" max_temporal_id=\"\" layer_count=\"\" minPicWidth=\"\" minPicHeight=\"\" maxPicWidth=\"\" maxPicHeight=\"\" maxChromaFormat=\"\" maxBitDepth=\"\" frame_rate_info_flag=\"\" bit_rate_info_flag=\"\" avgFrameRate=\"\" constantFrameRate=\"\" maxBitRate=\"\" avgBitRate=\"\"/>\n");

		fprintf(trace, "<Layer dependent_layerID=\"\" num_layers_dependent_on=\"\" dependent_on_layerID=\"\" dimension_identifier=\"\"/>\n");
		fprintf(trace, "</OperatingPointsInformation>\n");
		return;
	}


	fprintf(trace, "<OperatingPointsInformation");
	fprintf(trace, " scalability_mask=\"%d (", ptr->scalability_mask);
	switch (ptr->scalability_mask) {
	case 2:
		fprintf(trace, "Multiview");
		break;
	case 4:
		fprintf(trace, "Spatial scalability");
		break;
	case 8:
		fprintf(trace, "Auxilary");
		break;
	default:
		fprintf(trace, "unknown");
	}
	fprintf(trace, ")\" num_profile_tier_level=\"%d\"", gf_list_count(ptr->profile_tier_levels) );
	fprintf(trace, " num_operating_points=\"%d\" dependency_layers=\"%d\"", gf_list_count(ptr->operating_points), gf_list_count(ptr->dependency_layers));
	fprintf(trace, ">\n");


	count=gf_list_count(ptr->profile_tier_levels);
	for (i = 0; i < count; i++) {
		LHEVC_ProfileTierLevel *ptl = (LHEVC_ProfileTierLevel *)gf_list_get(ptr->profile_tier_levels, i);
		fprintf(trace, " <ProfileTierLevel general_profile_space=\"%d\" general_tier_flag=\"%d\" general_profile_idc=\"%d\" general_profile_compatibility_flags=\"%d\" general_constraint_indicator_flags=\""LLU"\" />\n", ptl->general_profile_space, ptl->general_tier_flag, ptl->general_profile_idc, ptl->general_profile_compatibility_flags, ptl->general_constraint_indicator_flags);
	}


	count=gf_list_count(ptr->operating_points);
	for (i = 0; i < count; i++) {
		LHEVC_OperatingPoint *op = (LHEVC_OperatingPoint *)gf_list_get(ptr->operating_points, i);
		fprintf(trace, "<OperatingPoint output_layer_set_idx=\"%d\"", op->output_layer_set_idx);
		fprintf(trace, " max_temporal_id=\"%d\" layer_count=\"%d\"", op->max_temporal_id, op->layer_count);
		fprintf(trace, " minPicWidth=\"%d\" minPicHeight=\"%d\"", op->minPicWidth, op->minPicHeight);
		fprintf(trace, " maxPicWidth=\"%d\" maxPicHeight=\"%d\"", op->maxPicWidth, op->maxPicHeight);
		fprintf(trace, " maxChromaFormat=\"%d\" maxBitDepth=\"%d\"", op->maxChromaFormat, op->maxBitDepth);
		fprintf(trace, " frame_rate_info_flag=\"%d\" bit_rate_info_flag=\"%d\"", op->frame_rate_info_flag, op->bit_rate_info_flag);
		if (op->frame_rate_info_flag) 
			fprintf(trace, " avgFrameRate=\"%d\" constantFrameRate=\"%d\"", op->avgFrameRate, op->constantFrameRate);
		if (op->bit_rate_info_flag) 
			fprintf(trace, " maxBitRate=\"%d\" avgBitRate=\"%d\"", op->maxBitRate, op->avgBitRate);
		fprintf(trace, "/>\n");
	}
	count=gf_list_count(ptr->dependency_layers);
	for (i = 0; i < count; i++) {
		u32 j;
		LHEVC_DependentLayer *dep = (LHEVC_DependentLayer *)gf_list_get(ptr->dependency_layers, i);
		fprintf(trace, "<Layer dependent_layerID=\"%d\" num_layers_dependent_on=\"%d\"", dep->dependent_layerID, dep->num_layers_dependent_on);
		if (dep->num_layers_dependent_on) {
			fprintf(trace, " dependent_on_layerID=\"");
			for (j = 0; j < dep->num_layers_dependent_on; j++)
				fprintf(trace, "%d ", dep->dependent_on_layerID[j]);
			fprintf(trace, "\"");
		}
		fprintf(trace, " dimension_identifier=\"");
		for (j = 0; j < 16; j++)
			if (ptr->scalability_mask & (1 << j))
				fprintf(trace, "%d ", dep->dimension_identifier[j]);
		fprintf(trace, "\"/>\n");
	}
	fprintf(trace, "</OperatingPointsInformation>\n");
	return;
}

static void linf_dump(GF_LHVCLayerInformation *ptr, FILE * trace)
{
	u32 i, count;
	if (!ptr) {
		fprintf(trace, "<LayerInformation num_layers=\"\">\n");
		fprintf(trace, "<LayerInfoItem layer_id=\"\" min_temporalId=\"\" max_temporalId=\"\" sub_layer_presence_flags=\"\"/>\n");
		fprintf(trace, "</LayerInformation>\n");
		return;
	}

	count = gf_list_count(ptr->num_layers_in_track);
	fprintf(trace, "<LayerInformation num_layers=\"%d\">\n", count );
	for (i = 0; i < count; i++) {
		LHVCLayerInfoItem *li = (LHVCLayerInfoItem *)gf_list_get(ptr->num_layers_in_track, i);
		fprintf(trace, "<LayerInfoItem layer_id=\"%d\" min_temporalId=\"%d\" max_temporalId=\"%d\" sub_layer_presence_flags=\"%d\"/>\n", li->layer_id, li->min_TemporalId, li->max_TemporalId, li->sub_layer_presence_flags);
	}
	fprintf(trace, "</LayerInformation>\n");
	return;
}

static void trif_dump(FILE * trace, char *data, u32 data_size)
{
	GF_BitStream *bs;
	u32 id, independent, filter_disabled;
	Bool full_picture, has_dep, tile_group;

	if (!data) {
		fprintf(trace, "<TileRegionGroupEntry ID=\"\" tileGroup=\"\" independent=\"\" full_picture=\"\" filter_disabled=\"\" x=\"\" y=\"\" w=\"\" h=\"\">\n");
		fprintf(trace, "<TileRegionDependency tileID=\"\"/>\n");
		fprintf(trace, "</TileRegionGroupEntry>\n");
		return;
	}

	bs = gf_bs_new(data, data_size, GF_BITSTREAM_READ);
	id = gf_bs_read_u16(bs);
	tile_group = gf_bs_read_int(bs, 1);
	fprintf(trace, "<TileRegionGroupEntry ID=\"%d\" tileGroup=\"%d\" ", id, tile_group);
	if (tile_group) {
		independent = gf_bs_read_int(bs, 2);
		full_picture = (Bool)gf_bs_read_int(bs, 1);
		filter_disabled = gf_bs_read_int(bs, 1);
		has_dep = gf_bs_read_int(bs, 1);
		gf_bs_read_int(bs, 2);
		fprintf(trace, "independent=\"%d\" full_picture=\"%d\" filter_disabled=\"%d\" ", independent, full_picture, filter_disabled);

		if (!full_picture) {
			fprintf(trace, "x=\"%d\" y=\"%d\" ", gf_bs_read_u16(bs), gf_bs_read_u16(bs));
		}
		fprintf(trace, "w=\"%d\" h=\"%d\" ", gf_bs_read_u16(bs), gf_bs_read_u16(bs));
		if (!has_dep) {
			fprintf(trace, "/>\n");
		} else {
			u32 count = gf_bs_read_u16(bs);
			fprintf(trace, ">\n");
			while (count) {
				count--;
				fprintf(trace, "<TileRegionDependency tileID=\"%d\"/>\n", gf_bs_read_u16(bs) );
			}
			fprintf(trace, "</TileRegionGroupEntry>\n");
		}
	}
	gf_bs_del(bs);
}

static void nalm_dump(FILE * trace, char *data, u32 data_size)
{
	GF_BitStream *bs;

	Bool rle, large_size;
	u32 entry_count;

	if (!data) {
		fprintf(trace, "<NALUMap rle=\"\" large_size=\"\">\n");
		fprintf(trace, "<NALUMapEntry NALU_startNumber=\"\" groupID=\"\"/>\n");
		fprintf(trace, "</NALUMap>\n");
		return;
	}
	
	bs = gf_bs_new(data, data_size, GF_BITSTREAM_READ);
	gf_bs_read_int(bs, 6);
	large_size = gf_bs_read_int(bs, 1);
	rle = gf_bs_read_int(bs, 1);
	entry_count = gf_bs_read_int(bs, large_size ? 16 : 8);
	fprintf(trace, "<NALUMap rle=\"%d\" large_size=\"%d\">\n", rle, large_size);
	
	while (entry_count) {
		u32 ID;
		fprintf(trace, "<NALUMapEntry ");
		if (rle) {
			u32 start_num = gf_bs_read_int(bs, large_size ? 16 : 8);
			fprintf(trace, "NALU_startNumber=\"%d\" ", start_num);
		}
		ID = gf_bs_read_u16(bs);
		fprintf(trace, "groupID=\"%d\"/>\n", ID);
		entry_count--;
	}
	gf_bs_del(bs);
	fprintf(trace, "</NALUMap>\n");
	return;
}


GF_Err sgpd_dump(GF_Box *a, FILE * trace)
{
	u32 i;
	GF_SampleGroupDescriptionBox *ptr = (GF_SampleGroupDescriptionBox*) a;
	if (!a) return GF_BAD_PARAM;

	DumpBox(a, "SampleGroupDescriptionBox", trace);
	gf_full_box_dump((GF_Box *)a, trace);
	fprintf(trace, "grouping_type=\"%s\"", gf_4cc_to_str(ptr->grouping_type) );
	if (ptr->version==1) fprintf(trace, " default_length=\"%d\"", ptr->default_length);
	if ((ptr->version>=2) && ptr->default_description_index) fprintf(trace, " default_group_index=\"%d\"", ptr->default_description_index);
	fprintf(trace, ">\n");
	for (i=0; i<gf_list_count(ptr->group_descriptions); i++) {
		void *entry = gf_list_get(ptr->group_descriptions, i);
		switch (ptr->grouping_type) {
		case GF_ISOM_SAMPLE_GROUP_ROLL:
			fprintf(trace, "<RollRecoveryEntry roll_distance=\"%d\" />\n", ((GF_RollRecoveryEntry*)entry)->roll_distance );
			break;
		case GF_ISOM_SAMPLE_GROUP_RAP:
			fprintf(trace, "<VisualRandomAccessEntry num_leading_samples_known=\"%s\"", ((GF_VisualRandomAccessEntry*)entry)->num_leading_samples_known ? "yes" : "no");
			if (((GF_VisualRandomAccessEntry*)entry)->num_leading_samples_known)
				fprintf(trace, " num_leading_samples=\"%d\"", ((GF_VisualRandomAccessEntry*)entry)->num_leading_samples);
			fprintf(trace, "/>\n");
			break;
		case GF_ISOM_SAMPLE_GROUP_SEIG:
			fprintf(trace, "<CENCSampleEncryptionGroupEntry IsEncrypted=\"%d\" IV_size=\"%d\" KID=\"", ((GF_CENCSampleEncryptionGroupEntry*)entry)->IsProtected, ((GF_CENCSampleEncryptionGroupEntry*)entry)->Per_Sample_IV_size);
			DumpDataHex(trace, (char *)((GF_CENCSampleEncryptionGroupEntry*)entry)->KID, 16);
			if ((((GF_CENCSampleEncryptionGroupEntry*)entry)->IsProtected == 1) && !((GF_CENCSampleEncryptionGroupEntry*)entry)->Per_Sample_IV_size) {
				fprintf(trace, "\" constant_IV_size=\"%d\"  constant_IV=\"", ((GF_CENCSampleEncryptionGroupEntry*)entry)->constant_IV_size);
				DumpDataHex(trace, (char *)((GF_CENCSampleEncryptionGroupEntry*)entry)->constant_IV, ((GF_CENCSampleEncryptionGroupEntry*)entry)->constant_IV_size);
			}
			fprintf(trace, "\"/>\n");
			break;
		case GF_ISOM_SAMPLE_GROUP_OINF:
			oinf_dump(entry, trace);
			break;
		case GF_ISOM_SAMPLE_GROUP_LINF:
			linf_dump(entry, trace);
			break;
		case GF_ISOM_SAMPLE_GROUP_TRIF:
			trif_dump(trace, (char *) ((GF_DefaultSampleGroupDescriptionEntry*)entry)->data,  ((GF_DefaultSampleGroupDescriptionEntry*)entry)->length);
			break;
			
		case GF_ISOM_SAMPLE_GROUP_NALM:
			nalm_dump(trace, (char *) ((GF_DefaultSampleGroupDescriptionEntry*)entry)->data,  ((GF_DefaultSampleGroupDescriptionEntry*)entry)->length);
			break;
		default:
			fprintf(trace, "<DefaultSampleGroupDescriptionEntry size=\"%d\" data=\"", ((GF_DefaultSampleGroupDescriptionEntry*)entry)->length);
			DumpData(trace, (char *) ((GF_DefaultSampleGroupDescriptionEntry*)entry)->data,  ((GF_DefaultSampleGroupDescriptionEntry*)entry)->length);
			fprintf(trace, "\"/>\n");
		}
	}
	if (!ptr->size) {
		switch (ptr->grouping_type) {
		case GF_ISOM_SAMPLE_GROUP_ROLL:
			fprintf(trace, "<RollRecoveryEntry roll_distance=\"\"/>\n");
			break;
		case GF_ISOM_SAMPLE_GROUP_RAP:
			fprintf(trace, "<VisualRandomAccessEntry num_leading_samples_known=\"yes|no\" num_leading_samples=\"\" />\n");
			break;
		case GF_ISOM_SAMPLE_GROUP_SEIG:
			fprintf(trace, "<CENCSampleEncryptionGroupEntry IsEncrypted=\"\" IV_size=\"\" KID=\"\" constant_IV_size=\"\"  constant_IV=\"\"/>\n");
			break;
		case GF_ISOM_SAMPLE_GROUP_OINF:
			oinf_dump(NULL, trace);
			break;
		case GF_ISOM_SAMPLE_GROUP_LINF:
			linf_dump(NULL, trace);
			break;
		case GF_ISOM_SAMPLE_GROUP_TRIF:
			trif_dump(trace, NULL, 0);
			break;
		case GF_ISOM_SAMPLE_GROUP_NALM:
			nalm_dump(trace, NULL, 0);
			break;
		default:
			fprintf(trace, "<DefaultSampleGroupDescriptionEntry size=\"\" data=\"\"/>\n");
		}
	}

	gf_box_dump_done("SampleGroupDescriptionBox", a, trace);
	return GF_OK;
}

GF_Err saiz_dump(GF_Box *a, FILE * trace)
{
	u32 i;
	GF_SampleAuxiliaryInfoSizeBox *ptr = (GF_SampleAuxiliaryInfoSizeBox*) a;
	if (!a) return GF_BAD_PARAM;

	DumpBox(a, "SampleAuxiliaryInfoSizeBox", trace);
	gf_full_box_dump((GF_Box *)a, trace);
	fprintf(trace, "default_sample_info_size=\"%d\" sample_count=\"%d\"", ptr->default_sample_info_size, ptr->sample_count);
	if (ptr->flags & 1) {
		if (isalnum(ptr->aux_info_type>>24)) {
			fprintf(trace, " aux_info_type=\"%s\" aux_info_type_parameter=\"%d\"", gf_4cc_to_str(ptr->aux_info_type), ptr->aux_info_type_parameter);
		} else {
			fprintf(trace, " aux_info_type=\"%d\" aux_info_type_parameter=\"%d\"", ptr->aux_info_type, ptr->aux_info_type_parameter);
		}
	}
	fprintf(trace, ">\n");
	if (ptr->default_sample_info_size==0) {
		for (i=0; i<ptr->sample_count; i++) {
			fprintf(trace, "<SAISize size=\"%d\" />\n", ptr->sample_info_size[i]);
		}
	}
	if (!ptr->size) {
			fprintf(trace, "<SAISize size=\"\" />\n");
	}
	gf_box_dump_done("SampleAuxiliaryInfoSizeBox", a, trace);
	return GF_OK;
}

GF_Err saio_dump(GF_Box *a, FILE * trace)
{
	u32 i;
	GF_SampleAuxiliaryInfoOffsetBox *ptr = (GF_SampleAuxiliaryInfoOffsetBox*) a;
	if (!a) return GF_BAD_PARAM;

	DumpBox(a, "SampleAuxiliaryInfoOffsetBox", trace);
	gf_full_box_dump((GF_Box *)a, trace);
	fprintf(trace, "entry_count=\"%d\"", ptr->entry_count);
	if (ptr->flags & 1) {
		if (isalnum(ptr->aux_info_type>>24)) {
			fprintf(trace, " aux_info_type=\"%s\" aux_info_type_parameter=\"%d\"", gf_4cc_to_str(ptr->aux_info_type), ptr->aux_info_type_parameter);
		} else {
			fprintf(trace, " aux_info_type=\"%d\" aux_info_type_parameter=\"%d\"", ptr->aux_info_type, ptr->aux_info_type_parameter);
		}
	}

	fprintf(trace, ">\n");

	if (ptr->version==0) {
		for (i=0; i<ptr->entry_count; i++) {
			fprintf(trace, "<SAIChunkOffset offset=\"%d\"/>\n", ptr->offsets[i]);
		}
	} else {
		for (i=0; i<ptr->entry_count; i++) {
			fprintf(trace, "<SAIChunkOffset offset=\""LLD"\"/>\n", ptr->offsets_large[i]);
		}
	}
	if (!ptr->size) {
			fprintf(trace, "<SAIChunkOffset offset=\"\"/>\n");
	}
	gf_box_dump_done("SampleAuxiliaryInfoOffsetBox", a, trace);
	return GF_OK;
}

GF_Err pssh_dump(GF_Box *a, FILE * trace)
{
	GF_ProtectionSystemHeaderBox *ptr = (GF_ProtectionSystemHeaderBox*) a;
	if (!a) return GF_BAD_PARAM;

	DumpBox(a, "ProtectionSystemHeaderBox", trace);
	gf_full_box_dump((GF_Box *)a, trace);
	fprintf(trace, "SystemID=\"");
	DumpDataHex(trace, (char *) ptr->SystemID, 16);
	fprintf(trace, "\">\n");

	if (ptr->KID_count) {
		u32 i;
		for (i=0; i<ptr->KID_count; i++) {
			fprintf(trace, " <PSSHKey KID=\"");
			DumpDataHex(trace, (char *) ptr->KIDs[i], 16);
			fprintf(trace, "\"/>\n");
		}
	}
	if (ptr->private_data_size) {
		fprintf(trace, " <PSSHData size=\"%d\" value=\"", ptr->private_data_size);
		DumpDataHex(trace, (char *) ptr->private_data, ptr->private_data_size);
		fprintf(trace, "\"/>\n");
	}
	if (!ptr->size) {
		fprintf(trace, " <PSSHKey KID=\"\"/>\n");
		fprintf(trace, " <PSSHData size=\"\" value=\"\"/>\n");
	}
	gf_box_dump_done("ProtectionSystemHeaderBox", a, trace);
	return GF_OK;
}

GF_Err tenc_dump(GF_Box *a, FILE * trace)
{
	GF_TrackEncryptionBox *ptr = (GF_TrackEncryptionBox*) a;
	if (!a) return GF_BAD_PARAM;

	DumpBox(a, "TrackEncryptionBox", trace);
	gf_full_box_dump((GF_Box *)a, trace);
	fprintf(trace, "isEncrypted=\"%d\"", ptr->isProtected);
	if (ptr->Per_Sample_IV_Size)
		fprintf(trace, " IV_size=\"%d\" KID=\"", ptr->Per_Sample_IV_Size);
	else {
		fprintf(trace, " constant_IV_size=\"%d\" constant_IV=\"", ptr->constant_IV_size);
		DumpDataHex(trace, (char *) ptr->KID, 16);
		fprintf(trace, "\"  KID=\"");
	}
	DumpDataHex(trace, (char *) ptr->KID, 16);
	if (ptr->version) 
		fprintf(trace, "\" crypt_byte_block=\"%d\" skip_byte_block=\"%d", ptr->crypt_byte_block, ptr->skip_byte_block);
	fprintf(trace, "\">\n");
	gf_box_dump_done("TrackEncryptionBox", a, trace);
	return GF_OK;
}

GF_Err piff_pssh_dump(GF_Box *a, FILE * trace)
{
	GF_PIFFProtectionSystemHeaderBox *ptr = (GF_PIFFProtectionSystemHeaderBox*) a;
	if (!a) return GF_BAD_PARAM;

	DumpBox(a, "PIFFProtectionSystemHeaderBox", trace);
	gf_full_box_dump(a, trace);
	fprintf(trace, "SystemID=\"");
	DumpDataHex(trace, (char *) ptr->SystemID, 16);
	fprintf(trace, "\" PrivateData=\"");
	DumpDataHex(trace, (char *) ptr->private_data, ptr->private_data_size);
	fprintf(trace, "\">\n");
	gf_box_dump_done("PIFFProtectionSystemHeaderBox", a, trace);
	return GF_OK;
}

GF_Err piff_tenc_dump(GF_Box *a, FILE * trace)
{
	GF_PIFFTrackEncryptionBox *ptr = (GF_PIFFTrackEncryptionBox*) a;
	if (!a) return GF_BAD_PARAM;

	DumpBox(a, "PIFFTrackEncryptionBox", trace);
	gf_full_box_dump(a, trace);
	fprintf(trace, "AlgorithmID=\"%d\" IV_size=\"%d\" KID=\"", ptr->AlgorithmID, ptr->IV_size);
	DumpDataHex(trace,(char *) ptr->KID, 16);
	fprintf(trace, "\">\n");
	gf_box_dump_done("PIFFTrackEncryptionBox", a, trace);
	return GF_OK;
}

GF_Err piff_psec_dump(GF_Box *a, FILE * trace)
{
	u32 i, j, sample_count;
	GF_PIFFSampleEncryptionBox *ptr = (GF_PIFFSampleEncryptionBox *) a;
	if (!a) return GF_BAD_PARAM;

	DumpBox(a, "PIFFSampleEncryptionBox", trace);
	sample_count = gf_list_count(ptr->samp_aux_info);
	fprintf(trace, "sampleCount=\"%d\"", sample_count);
	if (ptr->flags & 1) {
		fprintf(trace, " AlgorithmID=\"%d\" IV_size=\"%d\" KID=\"", ptr->AlgorithmID, ptr->IV_size);
		DumpData(trace, (char *) ptr->KID, 16);
		fprintf(trace, "\"");
	}
	fprintf(trace, ">\n");

	if (sample_count) {
		for (i=0; i<sample_count; i++) {
			GF_CENCSampleAuxInfo *cenc_sample = (GF_CENCSampleAuxInfo *)gf_list_get(ptr->samp_aux_info, i);

			if (cenc_sample) {
				if  (!strlen((char *)cenc_sample->IV)) continue;
				fprintf(trace, "<PIFFSampleEncryptionEntry IV=\"");
				DumpDataHex(trace, (char *) cenc_sample->IV, 16);
				if (ptr->flags & 0x2) {
					fprintf(trace, "\" SubsampleCount=\"%d\"", cenc_sample->subsample_count);
					fprintf(trace, ">\n");

					for (j=0; j<cenc_sample->subsample_count; j++) {
						fprintf(trace, "<PIFFSubSampleEncryptionEntry NumClearBytes=\"%d\" NumEncryptedBytes=\"%d\"/>\n", cenc_sample->subsamples[j].bytes_clear_data, cenc_sample->subsamples[j].bytes_encrypted_data);
					}
				}
				fprintf(trace, "</PIFFSampleEncryptionEntry>\n");
			}
		}
	}
	if (!ptr->size) {
		fprintf(trace, "<PIFFSampleEncryptionEntry IV=\"\" SubsampleCount=\"\">\n");
		fprintf(trace, "<PIFFSubSampleEncryptionEntry NumClearBytes=\"\" NumEncryptedBytes=\"\"/>\n");
		fprintf(trace, "</PIFFSampleEncryptionEntry>\n");
	}
	gf_box_dump_done("PIFFSampleEncryptionBox", a, trace);
	return GF_OK;
}

GF_Err senc_dump(GF_Box *a, FILE * trace)
{
	u32 i, j, sample_count;
	GF_SampleEncryptionBox *ptr = (GF_SampleEncryptionBox *) a;
	if (!a) return GF_BAD_PARAM;

	DumpBox(a, "SampleEncryptionBox", trace);
	sample_count = gf_list_count(ptr->samp_aux_info);
	fprintf(trace, "sampleCount=\"%d\">\n", sample_count);
	//WARNING - PSEC (UUID) IS TYPECASTED TO SENC (FULL BOX) SO WE CANNOT USE USUAL FULL BOX FUNCTIONS
	fprintf(trace, "<FullBoxInfo Version=\"%d\" Flags=\"0x%X\"/>\n", ptr->version, ptr->flags);
	for (i=0; i<sample_count; i++) {
		GF_CENCSampleAuxInfo *cenc_sample = (GF_CENCSampleAuxInfo *)gf_list_get(ptr->samp_aux_info, i);

		if (cenc_sample) {
			fprintf(trace, "<SampleEncryptionEntry sampleCount=\"%d\" IV=\"", i+1);
			DumpDataHex(trace, (char *) cenc_sample->IV, 16);
			fprintf(trace, "\"");
			if (ptr->flags & 0x2) {
				fprintf(trace, " SubsampleCount=\"%d\"", cenc_sample->subsample_count);
				fprintf(trace, ">\n");

				for (j=0; j<cenc_sample->subsample_count; j++) {
					fprintf(trace, "<SubSampleEncryptionEntry NumClearBytes=\"%d\" NumEncryptedBytes=\"%d\"/>\n", cenc_sample->subsamples[j].bytes_clear_data, cenc_sample->subsamples[j].bytes_encrypted_data);
				}
			} else {
				fprintf(trace, ">\n");
			}
			fprintf(trace, "</SampleEncryptionEntry>\n");
		}
	}
	if (!ptr->size) {
		fprintf(trace, "<SampleEncryptionEntry sampleCount=\"\" IV=\"\" SubsampleCount=\"\">\n");
		fprintf(trace, "<SubSampleEncryptionEntry NumClearBytes=\"\" NumEncryptedBytes=\"\"/>\n");
		fprintf(trace, "</SampleEncryptionEntry>\n");
	}
	gf_box_dump_done("SampleEncryptionBox", a, trace);
	return GF_OK;
}

GF_Err prft_dump(GF_Box *a, FILE * trace)
{
	Double fracs;
	GF_ProducerReferenceTimeBox *ptr = (GF_ProducerReferenceTimeBox *) a;
	time_t secs = (ptr->ntp >> 32) - GF_NTP_SEC_1900_TO_1970;
	struct tm t = *gmtime(&secs);
	fracs = (Double) (ptr->ntp & 0xFFFFFFFFULL);
	fracs /= 0xFFFFFFFF;
	fracs *= 1000;
	DumpBox(a, "ProducerReferenceTimeBox", trace);
	gf_full_box_dump((GF_Box *)a, trace);
	fprintf(trace, "referenceTrackID=\"%d\" timestamp=\""LLU"\" NTP=\""LLU"\" UTC=\"%d-%02d-%02dT%02d:%02d:%02d.%03dZ\">\n", ptr->refTrackID, ptr->timestamp, ptr->ntp, 1900+t.tm_year, t.tm_mon+1, t.tm_mday, t.tm_hour, t.tm_min, (u32) t.tm_sec, (u32) fracs);
	gf_box_dump_done("ProducerReferenceTimeBox", a, trace);

	return GF_OK;
}

GF_Err adkm_dump(GF_Box *a, FILE * trace)
{
	GF_AdobeDRMKeyManagementSystemBox *ptr = (GF_AdobeDRMKeyManagementSystemBox *)a;
	if (!a) return GF_BAD_PARAM;
	DumpBox(a, "AdobeDRMKeyManagementSystemBox", trace);
	gf_full_box_dump((GF_Box *)a, trace);
	fprintf(trace, ">\n");
	if (ptr->header) gf_box_dump((GF_Box *)ptr->header, trace);
	if (ptr->au_format) gf_box_dump((GF_Box *)ptr->au_format, trace);
	gf_box_dump_done("AdobeDRMKeyManagementSystemBox", a, trace);
	return GF_OK;
}

GF_Err ahdr_dump(GF_Box *a, FILE * trace)
{
	GF_AdobeDRMHeaderBox *ptr = (GF_AdobeDRMHeaderBox *)a;
	if (!a) return GF_BAD_PARAM;
	DumpBox(a, "AdobeDRMHeaderBox", trace);
	gf_full_box_dump((GF_Box *)a, trace);
	fprintf(trace, ">\n");
	if (ptr->std_enc_params) gf_box_dump((GF_Box *)ptr->std_enc_params, trace);
	gf_box_dump_done("AdobeDRMHeaderBox", a, trace);
	return GF_OK;
}

GF_Err aprm_dump(GF_Box *a, FILE * trace)
{
	GF_AdobeStdEncryptionParamsBox *ptr = (GF_AdobeStdEncryptionParamsBox *)a;
	if (!a) return GF_BAD_PARAM;
	DumpBox(a, "AdobeStdEncryptionParamsBox", trace);
	gf_full_box_dump((GF_Box *)a, trace);
	fprintf(trace, ">\n");
	if (ptr->enc_info) gf_box_dump((GF_Box *)ptr->enc_info, trace);
	if (ptr->key_info) gf_box_dump((GF_Box *)ptr->key_info, trace);
	gf_box_dump_done("AdobeStdEncryptionParamsBox", a, trace);
	return GF_OK;
}

GF_Err aeib_dump(GF_Box *a, FILE * trace)
{
	GF_AdobeEncryptionInfoBox *ptr = (GF_AdobeEncryptionInfoBox *)a;
	if (!a) return GF_BAD_PARAM;
	DumpBox(a, "AdobeEncryptionInfoBox", trace);
	gf_full_box_dump((GF_Box *)a, trace);
	fprintf(trace, "EncryptionAlgorithm=\"%s\" KeyLength=\"%d\">\n", ptr->enc_algo, ptr->key_length);
	gf_box_dump_done("AdobeEncryptionInfoBox", a, trace);
	return GF_OK;
}

GF_Err akey_dump(GF_Box *a, FILE * trace)
{
	GF_AdobeKeyInfoBox *ptr = (GF_AdobeKeyInfoBox *)a;
	if (!a) return GF_BAD_PARAM;
	DumpBox(a, "AdobeKeyInfoBox", trace);
	gf_full_box_dump((GF_Box *)a, trace);
	fprintf(trace, ">\n");
	if (ptr->params) gf_box_dump((GF_Box *)ptr->params, trace);
	gf_box_dump_done("AdobeKeyInfoBox", a, trace);
	return GF_OK;
}

GF_Err flxs_dump(GF_Box *a, FILE * trace)
{
	GF_AdobeFlashAccessParamsBox *ptr = (GF_AdobeFlashAccessParamsBox *)a;
	if (!a) return GF_BAD_PARAM;
	DumpBox(a, "AdobeFlashAccessParamsBox", trace);
	fprintf(trace, ">\n");
	if (ptr->metadata)
		fprintf(trace, "<FmrmsV2Metadata=\"%s\"/>\n", ptr->metadata);
	gf_box_dump_done("AdobeFlashAccessParamsBox", a, trace);
	return GF_OK;
}

GF_Err adaf_dump(GF_Box *a, FILE * trace)
{
	GF_AdobeDRMAUFormatBox *ptr = (GF_AdobeDRMAUFormatBox *)a;
	if (!a) return GF_BAD_PARAM;
	DumpBox(a, "AdobeDRMAUFormatBox ", trace);
	gf_full_box_dump((GF_Box *)a, trace);
	fprintf(trace, "SelectiveEncryption=\"%d\" IV_length=\"%d\">\n", ptr->selective_enc ? 1 : 0, ptr->IV_length);
	gf_box_dump_done("AdobeDRMAUFormatBox", a, trace);
	return GF_OK;
}

/* Image File Format dump */
GF_Err ispe_dump(GF_Box *a, FILE * trace)
{
	GF_ImageSpatialExtentsPropertyBox *ptr = (GF_ImageSpatialExtentsPropertyBox *)a;
	if (!a) return GF_BAD_PARAM;
	DumpBox(a, "ImageSpatialExtentsPropertyBox", trace);
	gf_full_box_dump((GF_Box *)a, trace);
	fprintf(trace, "image_width=\"%d\" image_height=\"%d\">\n", ptr->image_width, ptr->image_height);
	gf_box_dump_done("ImageSpatialExtentsPropertyBox", a, trace);
	return GF_OK;
}

GF_Err colr_dump(GF_Box *a, FILE * trace)
{
	GF_ColourInformationBox *ptr = (GF_ColourInformationBox *)a;
	if (!a) return GF_BAD_PARAM;
	DumpBox(a, "ColourInformationBox", trace);
	gf_full_box_dump((GF_Box *)a, trace);
	fprintf(trace, "colour_type=\"%s\" colour_primaries=\"%d\" transfer_characteristics=\"%d\" matrix_coefficients=\"%d\" full_range_flag=\"%d\">\n", gf_4cc_to_str(ptr->colour_type), ptr->colour_primaries, ptr->transfer_characteristics, ptr->matrix_coefficients, ptr->full_range_flag);
	gf_box_dump_done("ColourInformationBox", a, trace);
	return GF_OK;
}

GF_Err pixi_dump(GF_Box *a, FILE * trace)
{
	u32 i;
	GF_PixelInformationPropertyBox *ptr = (GF_PixelInformationPropertyBox *)a;
	if (!a) return GF_BAD_PARAM;
	DumpBox(a, "PixelInformationPropertyBox", trace);
	gf_full_box_dump((GF_Box *)a, trace);
	fprintf(trace, "num_channels=\"%d\" bits_per_channel=\"", ptr->num_channels);
	for (i = 0; i < ptr->num_channels; i++) {
		if (i != 0) fprintf(trace, ", ");
		fprintf(trace, "%d", ptr->bits_per_channel[i]);
	}
	fprintf(trace, "\">\n");
	gf_box_dump_done("PixelInformationPropertyBox", a, trace);
	return GF_OK;
}

GF_Err rloc_dump(GF_Box *a, FILE * trace)
{
	GF_RelativeLocationPropertyBox *ptr = (GF_RelativeLocationPropertyBox *)a;
	if (!a) return GF_BAD_PARAM;
	DumpBox(a, "RelativeLocationPropertyBox", trace);
	gf_full_box_dump((GF_Box *)a, trace);
	fprintf(trace, "horizontal_offset=\"%d\" vertical_offset=\"%d\">\n", ptr->horizontal_offset, ptr->vertical_offset);
	gf_box_dump_done("RelativeLocationPropertyBox", a, trace);
	return GF_OK;
}

GF_Err irot_dump(GF_Box *a, FILE * trace)
{
	GF_ImageRotationBox *ptr = (GF_ImageRotationBox *)a;
	if (!a) return GF_BAD_PARAM;
	DumpBox(a, "ImageRotationBox", trace);
	gf_full_box_dump((GF_Box *)a, trace);
	fprintf(trace, "angle=\"%d\">\n", (ptr->angle*90));
	gf_box_dump_done("ImageRotationBox", a, trace);
	return GF_OK;
}

GF_Err ipco_dump(GF_Box *a, FILE * trace)
{
	DumpBox(a, "ItemPropertyContainerBox", trace);
	fprintf(trace, ">\n");
	gf_box_dump_done("ItemPropertyContainerBox", a, trace);
	return GF_OK;
}

GF_Err iprp_dump(GF_Box *a, FILE * trace)
{
	GF_ItemPropertiesBox *ptr = (GF_ItemPropertiesBox *)a;
	DumpBox(a, "ItemPropertiesBox", trace);
	fprintf(trace, ">\n");
	if (ptr->property_container) gf_box_dump(ptr->property_container, trace);
	gf_box_dump_done("ItemPropertiesBox", a, trace);
	return GF_OK;
}

GF_Err ipma_dump(GF_Box *a, FILE * trace)
{
	u32 i, j;
	GF_ItemPropertyAssociationBox *ptr = (GF_ItemPropertyAssociationBox *)a;
	u32 entry_count = gf_list_count(ptr->entries);
	if (!a) return GF_BAD_PARAM;
	DumpBox(a, "ItemPropertyAssociationBox", trace);
	gf_full_box_dump((GF_Box *)a, trace);
	fprintf(trace, "entry_count=\"%d\">\n", entry_count);
	for (i = 0; i < entry_count; i++) {
		GF_ItemPropertyAssociationEntry *entry = (GF_ItemPropertyAssociationEntry *)gf_list_get(ptr->entries, i);
		u32 association_count = gf_list_count(entry->essential);
		fprintf(trace, "<AssociationEntry item_ID=\"%d\" association_count=\"%d\">\n", entry->item_id, association_count);
		for (j = 0; j < association_count; j++) {
			Bool *ess = (Bool *)gf_list_get(entry->essential, j);
			u32 *prop_index = (u32 *)gf_list_get(entry->property_index, j);
			fprintf(trace, "<Property index=\"%d\" essential=\"%d\"/>\n", *prop_index, *ess);
		}
		fprintf(trace, "</AssociationEntry>\n");
	}
	if (!ptr->size) {
		fprintf(trace, "<AssociationEntry item_ID=\"\" association_count=\"\">\n");
		fprintf(trace, "<Property index=\"\" essential=\"\"/>\n");
		fprintf(trace, "</AssociationEntry>\n");
	}
	gf_box_dump_done("ItemPropertyAssociationBox", a, trace);
	return GF_OK;
}

GF_Err trgr_dump(GF_Box *a, FILE * trace)
{
	GF_TrackGroupBox *ptr = (GF_TrackGroupBox *) a;
	DumpBox(a, "TrackGroupBox", trace);
	fprintf(trace, ">\n");
	gf_box_array_dump(ptr->groups, trace);
	gf_box_dump_done("TrackGroupBox", a, trace);
	return GF_OK;
}

GF_Err trgt_dump(GF_Box *a, FILE * trace)
{
	GF_TrackGroupTypeBox *ptr = (GF_TrackGroupTypeBox *) a;
	a->type = ptr->group_type;
	DumpBox(a, "TrackGroupTypeBox", trace);
	a->type = GF_ISOM_BOX_TYPE_TRGT;
	gf_full_box_dump((GF_Box *)a, trace);
	fprintf(trace, "track_group_id=\"%d\">\n", ptr->track_group_id);
	gf_box_dump_done("TrackGroupTypeBox", a, trace);
	return GF_OK;
}

GF_Err grpl_dump(GF_Box *a, FILE * trace)
{
	DumpBox(a, "GroupListBox", trace);
	fprintf(trace, ">\n");
	gf_box_dump_done("GroupListBox", a, trace);
	return GF_OK;
}

#endif /*GPAC_DISABLE_ISOM_DUMP*/
