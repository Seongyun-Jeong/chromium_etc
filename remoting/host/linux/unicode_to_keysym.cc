// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/host/linux/unicode_to_keysym.h"

#include <algorithm>

#include "base/cxx17_backports.h"
#include "ui/gfx/x/keysyms/keysyms.h"

namespace remoting {

namespace {

struct CodePair {
  int keysym;
  uint32_t unicode;
};

// The table has been sorted by the second column so it can be searched using
// binary search. There might be multiple present keysyms for the same unicode
// value (e.g. see XK_Tab and XK_KP_Tab). It excludes Latin1 characters (which
// have 1-to-1 mapping between keysym and unicode), but includes some
// alternative keysyms for some of them (e.g. XK_KP_0 for '0').
const CodePair kKeySymUnicodeMap[] = {
  { XK_BackSpace,                   0x0008 },
  { XK_Tab,                         0x0009 },
  { XK_KP_Tab,                      0x0009 },
  { XK_Return,                      0x000a },
  { XK_Escape,                      0x001b },
  { XK_KP_Multiply,                 0x002a },
  { XK_KP_Add,                      0x002b },
  { XK_KP_Separator,                0x002c },
  { XK_KP_Subtract,                 0x002d },
  { XK_KP_Decimal,                  0x002e },
  { XK_KP_Divide,                   0x002f },
  { XK_KP_0,                        0x0030 },
  { XK_KP_1,                        0x0031 },
  { XK_KP_2,                        0x0032 },
  { XK_KP_3,                        0x0033 },
  { XK_KP_4,                        0x0034 },
  { XK_KP_5,                        0x0035 },
  { XK_KP_6,                        0x0036 },
  { XK_KP_7,                        0x0037 },
  { XK_KP_8,                        0x0038 },
  { XK_KP_9,                        0x0039 },
  { XK_leftcaret,                   0x003c },
  { XK_KP_Equal,                    0x003d },
  { XK_rightcaret,                  0x003e },
  { XK_underbar,                    0x005f },
  { XK_Delete,                      0x007f },
  { XK_overbar,                     0x00af },
  { XK_Amacron,                     0x0100 },
  { XK_amacron,                     0x0101 },
  { XK_Abreve,                      0x0102 },
  { XK_abreve,                      0x0103 },
  { XK_Aogonek,                     0x0104 },
  { XK_aogonek,                     0x0105 },
  { XK_Cacute,                      0x0106 },
  { XK_cacute,                      0x0107 },
  { XK_Ccircumflex,                 0x0108 },
  { XK_ccircumflex,                 0x0109 },
  { XK_Cabovedot,                   0x010a },
  { XK_cabovedot,                   0x010b },
  { XK_Ccaron,                      0x010c },
  { XK_ccaron,                      0x010d },
  { XK_Dcaron,                      0x010e },
  { XK_dcaron,                      0x010f },
  { XK_Dstroke,                     0x0110 },
  { XK_dstroke,                     0x0111 },
  { XK_Emacron,                     0x0112 },
  { XK_emacron,                     0x0113 },
  { XK_Eabovedot,                   0x0116 },
  { XK_eabovedot,                   0x0117 },
  { XK_Eogonek,                     0x0118 },
  { XK_eogonek,                     0x0119 },
  { XK_Ecaron,                      0x011a },
  { XK_ecaron,                      0x011b },
  { XK_Gcircumflex,                 0x011c },
  { XK_gcircumflex,                 0x011d },
  { XK_Gbreve,                      0x011e },
  { XK_gbreve,                      0x011f },
  { XK_Gabovedot,                   0x0120 },
  { XK_gabovedot,                   0x0121 },
  { XK_Gcedilla,                    0x0122 },
  { XK_gcedilla,                    0x0123 },
  { XK_Hcircumflex,                 0x0124 },
  { XK_hcircumflex,                 0x0125 },
  { XK_Hstroke,                     0x0126 },
  { XK_hstroke,                     0x0127 },
  { XK_Itilde,                      0x0128 },
  { XK_itilde,                      0x0129 },
  { XK_Imacron,                     0x012a },
  { XK_imacron,                     0x012b },
  { XK_Iogonek,                     0x012e },
  { XK_iogonek,                     0x012f },
  { XK_Iabovedot,                   0x0130 },
  { XK_idotless,                    0x0131 },
  { XK_Jcircumflex,                 0x0134 },
  { XK_jcircumflex,                 0x0135 },
  { XK_Kcedilla,                    0x0136 },
  { XK_kcedilla,                    0x0137 },
  { XK_kra,                         0x0138 },
  { XK_Lacute,                      0x0139 },
  { XK_lacute,                      0x013a },
  { XK_Lcedilla,                    0x013b },
  { XK_lcedilla,                    0x013c },
  { XK_Lcaron,                      0x013d },
  { XK_lcaron,                      0x013e },
  { XK_Lstroke,                     0x0141 },
  { XK_lstroke,                     0x0142 },
  { XK_Nacute,                      0x0143 },
  { XK_nacute,                      0x0144 },
  { XK_Ncedilla,                    0x0145 },
  { XK_ncedilla,                    0x0146 },
  { XK_Ncaron,                      0x0147 },
  { XK_ncaron,                      0x0148 },
  { XK_ENG,                         0x014a },
  { XK_eng,                         0x014b },
  { XK_Omacron,                     0x014c },
  { XK_omacron,                     0x014d },
  { XK_Odoubleacute,                0x0150 },
  { XK_odoubleacute,                0x0151 },
  { XK_OE,                          0x0152 },
  { XK_oe,                          0x0153 },
  { XK_Racute,                      0x0154 },
  { XK_racute,                      0x0155 },
  { XK_Rcedilla,                    0x0156 },
  { XK_rcedilla,                    0x0157 },
  { XK_Rcaron,                      0x0158 },
  { XK_rcaron,                      0x0159 },
  { XK_Sacute,                      0x015a },
  { XK_sacute,                      0x015b },
  { XK_Scircumflex,                 0x015c },
  { XK_scircumflex,                 0x015d },
  { XK_Scedilla,                    0x015e },
  { XK_scedilla,                    0x015f },
  { XK_Scaron,                      0x0160 },
  { XK_scaron,                      0x0161 },
  { XK_Tcedilla,                    0x0162 },
  { XK_tcedilla,                    0x0163 },
  { XK_Tcaron,                      0x0164 },
  { XK_tcaron,                      0x0165 },
  { XK_Tslash,                      0x0166 },
  { XK_tslash,                      0x0167 },
  { XK_Utilde,                      0x0168 },
  { XK_utilde,                      0x0169 },
  { XK_Umacron,                     0x016a },
  { XK_umacron,                     0x016b },
  { XK_Ubreve,                      0x016c },
  { XK_ubreve,                      0x016d },
  { XK_Uring,                       0x016e },
  { XK_uring,                       0x016f },
  { XK_Udoubleacute,                0x0170 },
  { XK_udoubleacute,                0x0171 },
  { XK_Uogonek,                     0x0172 },
  { XK_uogonek,                     0x0173 },
  { XK_Ydiaeresis,                  0x0178 },
  { XK_Zacute,                      0x0179 },
  { XK_zacute,                      0x017a },
  { XK_Zabovedot,                   0x017b },
  { XK_zabovedot,                   0x017c },
  { XK_Zcaron,                      0x017d },
  { XK_zcaron,                      0x017e },
  { XK_function,                    0x0192 },
  { XK_caron,                       0x02c7 },
  { XK_breve,                       0x02d8 },
  { XK_abovedot,                    0x02d9 },
  { XK_ogonek,                      0x02db },
  { XK_doubleacute,                 0x02dd },
  { XK_Greek_accentdieresis,        0x0385 },
  { XK_Greek_ALPHAaccent,           0x0386 },
  { XK_Greek_EPSILONaccent,         0x0388 },
  { XK_Greek_ETAaccent,             0x0389 },
  { XK_Greek_IOTAaccent,            0x038a },
  { XK_Greek_OMICRONaccent,         0x038c },
  { XK_Greek_UPSILONaccent,         0x038e },
  { XK_Greek_OMEGAaccent,           0x038f },
  { XK_Greek_iotaaccentdieresis,    0x0390 },
  { XK_Greek_ALPHA,                 0x0391 },
  { XK_Greek_BETA,                  0x0392 },
  { XK_Greek_GAMMA,                 0x0393 },
  { XK_Greek_DELTA,                 0x0394 },
  { XK_Greek_EPSILON,               0x0395 },
  { XK_Greek_ZETA,                  0x0396 },
  { XK_Greek_ETA,                   0x0397 },
  { XK_Greek_THETA,                 0x0398 },
  { XK_Greek_IOTA,                  0x0399 },
  { XK_Greek_KAPPA,                 0x039a },
  { XK_Greek_LAMDA,                 0x039b },
  { XK_Greek_MU,                    0x039c },
  { XK_Greek_NU,                    0x039d },
  { XK_Greek_XI,                    0x039e },
  { XK_Greek_OMICRON,               0x039f },
  { XK_Greek_PI,                    0x03a0 },
  { XK_Greek_RHO,                   0x03a1 },
  { XK_Greek_SIGMA,                 0x03a3 },
  { XK_Greek_TAU,                   0x03a4 },
  { XK_Greek_UPSILON,               0x03a5 },
  { XK_Greek_PHI,                   0x03a6 },
  { XK_Greek_CHI,                   0x03a7 },
  { XK_Greek_PSI,                   0x03a8 },
  { XK_Greek_OMEGA,                 0x03a9 },
  { XK_Greek_IOTAdiaeresis,         0x03aa },
  { XK_Greek_UPSILONdieresis,       0x03ab },
  { XK_Greek_alphaaccent,           0x03ac },
  { XK_Greek_epsilonaccent,         0x03ad },
  { XK_Greek_etaaccent,             0x03ae },
  { XK_Greek_iotaaccent,            0x03af },
  { XK_Greek_upsilonaccentdieresis, 0x03b0 },
  { XK_Greek_alpha,                 0x03b1 },
  { XK_Greek_beta,                  0x03b2 },
  { XK_Greek_gamma,                 0x03b3 },
  { XK_Greek_delta,                 0x03b4 },
  { XK_Greek_epsilon,               0x03b5 },
  { XK_Greek_zeta,                  0x03b6 },
  { XK_Greek_eta,                   0x03b7 },
  { XK_Greek_theta,                 0x03b8 },
  { XK_Greek_iota,                  0x03b9 },
  { XK_Greek_kappa,                 0x03ba },
  { XK_Greek_lamda,                 0x03bb },
  { XK_Greek_mu,                    0x03bc },
  { XK_Greek_nu,                    0x03bd },
  { XK_Greek_xi,                    0x03be },
  { XK_Greek_omicron,               0x03bf },
  { XK_Greek_pi,                    0x03c0 },
  { XK_Greek_rho,                   0x03c1 },
  { XK_Greek_finalsmallsigma,       0x03c2 },
  { XK_Greek_sigma,                 0x03c3 },
  { XK_Greek_tau,                   0x03c4 },
  { XK_Greek_upsilon,               0x03c5 },
  { XK_Greek_phi,                   0x03c6 },
  { XK_Greek_chi,                   0x03c7 },
  { XK_Greek_psi,                   0x03c8 },
  { XK_Greek_omega,                 0x03c9 },
  { XK_Greek_iotadieresis,          0x03ca },
  { XK_Greek_upsilondieresis,       0x03cb },
  { XK_Greek_omicronaccent,         0x03cc },
  { XK_Greek_upsilonaccent,         0x03cd },
  { XK_Greek_omegaaccent,           0x03ce },
  { XK_Cyrillic_IO,                 0x0401 },
  { XK_Serbian_DJE,                 0x0402 },
  { XK_Macedonia_GJE,               0x0403 },
  { XK_Ukrainian_IE,                0x0404 },
  { XK_Macedonia_DSE,               0x0405 },
  { XK_Ukrainian_I,                 0x0406 },
  { XK_Ukrainian_YI,                0x0407 },
  { XK_Cyrillic_JE,                 0x0408 },
  { XK_Cyrillic_LJE,                0x0409 },
  { XK_Cyrillic_NJE,                0x040a },
  { XK_Serbian_TSHE,                0x040b },
  { XK_Macedonia_KJE,               0x040c },
  { XK_Byelorussian_SHORTU,         0x040e },
  { XK_Cyrillic_DZHE,               0x040f },
  { XK_Cyrillic_A,                  0x0410 },
  { XK_Cyrillic_BE,                 0x0411 },
  { XK_Cyrillic_VE,                 0x0412 },
  { XK_Cyrillic_GHE,                0x0413 },
  { XK_Cyrillic_DE,                 0x0414 },
  { XK_Cyrillic_IE,                 0x0415 },
  { XK_Cyrillic_ZHE,                0x0416 },
  { XK_Cyrillic_ZE,                 0x0417 },
  { XK_Cyrillic_I,                  0x0418 },
  { XK_Cyrillic_SHORTI,             0x0419 },
  { XK_Cyrillic_KA,                 0x041a },
  { XK_Cyrillic_EL,                 0x041b },
  { XK_Cyrillic_EM,                 0x041c },
  { XK_Cyrillic_EN,                 0x041d },
  { XK_Cyrillic_O,                  0x041e },
  { XK_Cyrillic_PE,                 0x041f },
  { XK_Cyrillic_ER,                 0x0420 },
  { XK_Cyrillic_ES,                 0x0421 },
  { XK_Cyrillic_TE,                 0x0422 },
  { XK_Cyrillic_U,                  0x0423 },
  { XK_Cyrillic_EF,                 0x0424 },
  { XK_Cyrillic_HA,                 0x0425 },
  { XK_Cyrillic_TSE,                0x0426 },
  { XK_Cyrillic_CHE,                0x0427 },
  { XK_Cyrillic_SHA,                0x0428 },
  { XK_Cyrillic_SHCHA,              0x0429 },
  { XK_Cyrillic_HARDSIGN,           0x042a },
  { XK_Cyrillic_YERU,               0x042b },
  { XK_Cyrillic_SOFTSIGN,           0x042c },
  { XK_Cyrillic_E,                  0x042d },
  { XK_Cyrillic_YU,                 0x042e },
  { XK_Cyrillic_YA,                 0x042f },
  { XK_Cyrillic_a,                  0x0430 },
  { XK_Cyrillic_be,                 0x0431 },
  { XK_Cyrillic_ve,                 0x0432 },
  { XK_Cyrillic_ghe,                0x0433 },
  { XK_Cyrillic_de,                 0x0434 },
  { XK_Cyrillic_ie,                 0x0435 },
  { XK_Cyrillic_zhe,                0x0436 },
  { XK_Cyrillic_ze,                 0x0437 },
  { XK_Cyrillic_i,                  0x0438 },
  { XK_Cyrillic_shorti,             0x0439 },
  { XK_Cyrillic_ka,                 0x043a },
  { XK_Cyrillic_el,                 0x043b },
  { XK_Cyrillic_em,                 0x043c },
  { XK_Cyrillic_en,                 0x043d },
  { XK_Cyrillic_o,                  0x043e },
  { XK_Cyrillic_pe,                 0x043f },
  { XK_Cyrillic_er,                 0x0440 },
  { XK_Cyrillic_es,                 0x0441 },
  { XK_Cyrillic_te,                 0x0442 },
  { XK_Cyrillic_u,                  0x0443 },
  { XK_Cyrillic_ef,                 0x0444 },
  { XK_Cyrillic_ha,                 0x0445 },
  { XK_Cyrillic_tse,                0x0446 },
  { XK_Cyrillic_che,                0x0447 },
  { XK_Cyrillic_sha,                0x0448 },
  { XK_Cyrillic_shcha,              0x0449 },
  { XK_Cyrillic_hardsign,           0x044a },
  { XK_Cyrillic_yeru,               0x044b },
  { XK_Cyrillic_softsign,           0x044c },
  { XK_Cyrillic_e,                  0x044d },
  { XK_Cyrillic_yu,                 0x044e },
  { XK_Cyrillic_ya,                 0x044f },
  { XK_Cyrillic_io,                 0x0451 },
  { XK_Serbian_dje,                 0x0452 },
  { XK_Macedonia_gje,               0x0453 },
  { XK_Ukrainian_ie,                0x0454 },
  { XK_Macedonia_dse,               0x0455 },
  { XK_Ukrainian_i,                 0x0456 },
  { XK_Ukrainian_yi,                0x0457 },
  { XK_Cyrillic_je,                 0x0458 },
  { XK_Cyrillic_lje,                0x0459 },
  { XK_Cyrillic_nje,                0x045a },
  { XK_Serbian_tshe,                0x045b },
  { XK_Macedonia_kje,               0x045c },
  { XK_Byelorussian_shortu,         0x045e },
  { XK_Cyrillic_dzhe,               0x045f },
  { XK_hebrew_aleph,                0x05d0 },
  { XK_hebrew_bet,                  0x05d1 },
  { XK_hebrew_gimel,                0x05d2 },
  { XK_hebrew_dalet,                0x05d3 },
  { XK_hebrew_he,                   0x05d4 },
  { XK_hebrew_waw,                  0x05d5 },
  { XK_hebrew_zain,                 0x05d6 },
  { XK_hebrew_chet,                 0x05d7 },
  { XK_hebrew_tet,                  0x05d8 },
  { XK_hebrew_yod,                  0x05d9 },
  { XK_hebrew_finalkaph,            0x05da },
  { XK_hebrew_kaph,                 0x05db },
  { XK_hebrew_lamed,                0x05dc },
  { XK_hebrew_finalmem,             0x05dd },
  { XK_hebrew_mem,                  0x05de },
  { XK_hebrew_finalnun,             0x05df },
  { XK_hebrew_nun,                  0x05e0 },
  { XK_hebrew_samech,               0x05e1 },
  { XK_hebrew_ayin,                 0x05e2 },
  { XK_hebrew_finalpe,              0x05e3 },
  { XK_hebrew_pe,                   0x05e4 },
  { XK_hebrew_finalzade,            0x05e5 },
  { XK_hebrew_zade,                 0x05e6 },
  { XK_hebrew_qoph,                 0x05e7 },
  { XK_hebrew_resh,                 0x05e8 },
  { XK_hebrew_shin,                 0x05e9 },
  { XK_hebrew_taw,                  0x05ea },
  { XK_Arabic_comma,                0x060c },
  { XK_Arabic_semicolon,            0x061b },
  { XK_Arabic_question_mark,        0x061f },
  { XK_Arabic_hamza,                0x0621 },
  { XK_Arabic_maddaonalef,          0x0622 },
  { XK_Arabic_hamzaonalef,          0x0623 },
  { XK_Arabic_hamzaonwaw,           0x0624 },
  { XK_Arabic_hamzaunderalef,       0x0625 },
  { XK_Arabic_hamzaonyeh,           0x0626 },
  { XK_Arabic_alef,                 0x0627 },
  { XK_Arabic_beh,                  0x0628 },
  { XK_Arabic_tehmarbuta,           0x0629 },
  { XK_Arabic_teh,                  0x062a },
  { XK_Arabic_theh,                 0x062b },
  { XK_Arabic_jeem,                 0x062c },
  { XK_Arabic_hah,                  0x062d },
  { XK_Arabic_khah,                 0x062e },
  { XK_Arabic_dal,                  0x062f },
  { XK_Arabic_thal,                 0x0630 },
  { XK_Arabic_ra,                   0x0631 },
  { XK_Arabic_zain,                 0x0632 },
  { XK_Arabic_seen,                 0x0633 },
  { XK_Arabic_sheen,                0x0634 },
  { XK_Arabic_sad,                  0x0635 },
  { XK_Arabic_dad,                  0x0636 },
  { XK_Arabic_tah,                  0x0637 },
  { XK_Arabic_zah,                  0x0638 },
  { XK_Arabic_ain,                  0x0639 },
  { XK_Arabic_ghain,                0x063a },
  { XK_Arabic_tatweel,              0x0640 },
  { XK_Arabic_feh,                  0x0641 },
  { XK_Arabic_qaf,                  0x0642 },
  { XK_Arabic_kaf,                  0x0643 },
  { XK_Arabic_lam,                  0x0644 },
  { XK_Arabic_meem,                 0x0645 },
  { XK_Arabic_noon,                 0x0646 },
  { XK_Arabic_ha,                   0x0647 },
  { XK_Arabic_waw,                  0x0648 },
  { XK_Arabic_alefmaksura,          0x0649 },
  { XK_Arabic_yeh,                  0x064a },
  { XK_Arabic_fathatan,             0x064b },
  { XK_Arabic_dammatan,             0x064c },
  { XK_Arabic_kasratan,             0x064d },
  { XK_Arabic_fatha,                0x064e },
  { XK_Arabic_damma,                0x064f },
  { XK_Arabic_kasra,                0x0650 },
  { XK_Arabic_shadda,               0x0651 },
  { XK_Arabic_sukun,                0x0652 },
  { XK_Thai_kokai,                  0x0e01 },
  { XK_Thai_khokhai,                0x0e02 },
  { XK_Thai_khokhuat,               0x0e03 },
  { XK_Thai_khokhwai,               0x0e04 },
  { XK_Thai_khokhon,                0x0e05 },
  { XK_Thai_khorakhang,             0x0e06 },
  { XK_Thai_ngongu,                 0x0e07 },
  { XK_Thai_chochan,                0x0e08 },
  { XK_Thai_choching,               0x0e09 },
  { XK_Thai_chochang,               0x0e0a },
  { XK_Thai_soso,                   0x0e0b },
  { XK_Thai_chochoe,                0x0e0c },
  { XK_Thai_yoying,                 0x0e0d },
  { XK_Thai_dochada,                0x0e0e },
  { XK_Thai_topatak,                0x0e0f },
  { XK_Thai_thothan,                0x0e10 },
  { XK_Thai_thonangmontho,          0x0e11 },
  { XK_Thai_thophuthao,             0x0e12 },
  { XK_Thai_nonen,                  0x0e13 },
  { XK_Thai_dodek,                  0x0e14 },
  { XK_Thai_totao,                  0x0e15 },
  { XK_Thai_thothung,               0x0e16 },
  { XK_Thai_thothahan,              0x0e17 },
  { XK_Thai_thothong,               0x0e18 },
  { XK_Thai_nonu,                   0x0e19 },
  { XK_Thai_bobaimai,               0x0e1a },
  { XK_Thai_popla,                  0x0e1b },
  { XK_Thai_phophung,               0x0e1c },
  { XK_Thai_fofa,                   0x0e1d },
  { XK_Thai_phophan,                0x0e1e },
  { XK_Thai_fofan,                  0x0e1f },
  { XK_Thai_phosamphao,             0x0e20 },
  { XK_Thai_moma,                   0x0e21 },
  { XK_Thai_yoyak,                  0x0e22 },
  { XK_Thai_rorua,                  0x0e23 },
  { XK_Thai_ru,                     0x0e24 },
  { XK_Thai_loling,                 0x0e25 },
  { XK_Thai_lu,                     0x0e26 },
  { XK_Thai_wowaen,                 0x0e27 },
  { XK_Thai_sosala,                 0x0e28 },
  { XK_Thai_sorusi,                 0x0e29 },
  { XK_Thai_sosua,                  0x0e2a },
  { XK_Thai_hohip,                  0x0e2b },
  { XK_Thai_lochula,                0x0e2c },
  { XK_Thai_oang,                   0x0e2d },
  { XK_Thai_honokhuk,               0x0e2e },
  { XK_Thai_paiyannoi,              0x0e2f },
  { XK_Thai_saraa,                  0x0e30 },
  { XK_Thai_maihanakat,             0x0e31 },
  { XK_Thai_saraaa,                 0x0e32 },
  { XK_Thai_saraam,                 0x0e33 },
  { XK_Thai_sarai,                  0x0e34 },
  { XK_Thai_saraii,                 0x0e35 },
  { XK_Thai_saraue,                 0x0e36 },
  { XK_Thai_sarauee,                0x0e37 },
  { XK_Thai_sarau,                  0x0e38 },
  { XK_Thai_sarauu,                 0x0e39 },
  { XK_Thai_phinthu,                0x0e3a },
  { XK_Thai_baht,                   0x0e3f },
  { XK_Thai_sarae,                  0x0e40 },
  { XK_Thai_saraae,                 0x0e41 },
  { XK_Thai_sarao,                  0x0e42 },
  { XK_Thai_saraaimaimuan,          0x0e43 },
  { XK_Thai_saraaimaimalai,         0x0e44 },
  { XK_Thai_lakkhangyao,            0x0e45 },
  { XK_Thai_maiyamok,               0x0e46 },
  { XK_Thai_maitaikhu,              0x0e47 },
  { XK_Thai_maiek,                  0x0e48 },
  { XK_Thai_maitho,                 0x0e49 },
  { XK_Thai_maitri,                 0x0e4a },
  { XK_Thai_maichattawa,            0x0e4b },
  { XK_Thai_thanthakhat,            0x0e4c },
  { XK_Thai_nikhahit,               0x0e4d },
  { XK_Thai_leksun,                 0x0e50 },
  { XK_Thai_leknung,                0x0e51 },
  { XK_Thai_leksong,                0x0e52 },
  { XK_Thai_leksam,                 0x0e53 },
  { XK_Thai_leksi,                  0x0e54 },
  { XK_Thai_lekha,                  0x0e55 },
  { XK_Thai_lekhok,                 0x0e56 },
  { XK_Thai_lekchet,                0x0e57 },
  { XK_Thai_lekpaet,                0x0e58 },
  { XK_Thai_lekkao,                 0x0e59 },
  { XK_Hangul_J_Kiyeog,             0x11a8 },
  { XK_Hangul_J_SsangKiyeog,        0x11a9 },
  { XK_Hangul_J_KiyeogSios,         0x11aa },
  { XK_Hangul_J_Nieun,              0x11ab },
  { XK_Hangul_J_NieunJieuj,         0x11ac },
  { XK_Hangul_J_NieunHieuh,         0x11ad },
  { XK_Hangul_J_Dikeud,             0x11ae },
  { XK_Hangul_J_Rieul,              0x11af },
  { XK_Hangul_J_RieulKiyeog,        0x11b0 },
  { XK_Hangul_J_RieulMieum,         0x11b1 },
  { XK_Hangul_J_RieulPieub,         0x11b2 },
  { XK_Hangul_J_RieulSios,          0x11b3 },
  { XK_Hangul_J_RieulTieut,         0x11b4 },
  { XK_Hangul_J_RieulPhieuf,        0x11b5 },
  { XK_Hangul_J_RieulHieuh,         0x11b6 },
  { XK_Hangul_J_Mieum,              0x11b7 },
  { XK_Hangul_J_Pieub,              0x11b8 },
  { XK_Hangul_J_PieubSios,          0x11b9 },
  { XK_Hangul_J_Sios,               0x11ba },
  { XK_Hangul_J_SsangSios,          0x11bb },
  { XK_Hangul_J_Ieung,              0x11bc },
  { XK_Hangul_J_Jieuj,              0x11bd },
  { XK_Hangul_J_Cieuc,              0x11be },
  { XK_Hangul_J_Khieuq,             0x11bf },
  { XK_Hangul_J_Tieut,              0x11c0 },
  { XK_Hangul_J_Phieuf,             0x11c1 },
  { XK_Hangul_J_Hieuh,              0x11c2 },
  { XK_Hangul_J_PanSios,            0x11eb },
  { XK_Hangul_J_KkogjiDalrinIeung , 0x11f0 },
  { XK_Hangul_J_YeorinHieuh,        0x11f9 },
  { XK_enspace,                     0x2002 },
  { XK_emspace,                     0x2003 },
  { XK_em3space,                    0x2004 },
  { XK_em4space,                    0x2005 },
  { XK_digitspace,                  0x2007 },
  { XK_punctspace,                  0x2008 },
  { XK_thinspace,                   0x2009 },
  { XK_hairspace,                   0x200a },
  { XK_figdash,                     0x2012 },
  { XK_endash,                      0x2013 },
  { XK_emdash,                      0x2014 },
  { XK_Greek_horizbar,              0x2015 },
  { XK_hebrew_doublelowline,        0x2017 },
  { XK_leftsinglequotemark,         0x2018 },
  { XK_rightsinglequotemark,        0x2019 },
  { XK_singlelowquotemark,          0x201a },
  { XK_leftdoublequotemark,         0x201c },
  { XK_rightdoublequotemark,        0x201d },
  { XK_doublelowquotemark,          0x201e },
  { XK_dagger,                      0x2020 },
  { XK_doubledagger,                0x2021 },
  { XK_enfilledcircbullet,          0x2022 },
  { XK_doubbaselinedot,             0x2025 },
  { XK_ellipsis,                    0x2026 },
  { XK_minutes,                     0x2032 },
  { XK_seconds,                     0x2033 },
  { XK_caret,                       0x2038 },
  { XK_overline,                    0x203e },
  { XK_Korean_Won,                  0x20a9 },
  { XK_EuroSign,                    0x20ac },
  { XK_careof,                      0x2105 },
  { XK_numerosign,                  0x2116 },
  { XK_phonographcopyright,         0x2117 },
  { XK_prescription,                0x211e },
  { XK_trademark,                   0x2122 },
  { XK_onethird,                    0x2153 },
  { XK_twothirds,                   0x2154 },
  { XK_onefifth,                    0x2155 },
  { XK_twofifths,                   0x2156 },
  { XK_threefifths,                 0x2157 },
  { XK_fourfifths,                  0x2158 },
  { XK_onesixth,                    0x2159 },
  { XK_fivesixths,                  0x215a },
  { XK_oneeighth,                   0x215b },
  { XK_threeeighths,                0x215c },
  { XK_fiveeighths,                 0x215d },
  { XK_seveneighths,                0x215e },
  { XK_leftarrow,                   0x2190 },
  { XK_uparrow,                     0x2191 },
  { XK_rightarrow,                  0x2192 },
  { XK_downarrow,                   0x2193 },
  { XK_implies,                     0x21d2 },
  { XK_ifonlyif,                    0x21d4 },
  { XK_partialderivative,           0x2202 },
  { XK_nabla,                       0x2207 },
  { XK_jot,                         0x2218 },
  { XK_radical,                     0x221a },
  { XK_variation,                   0x221d },
  { XK_infinity,                    0x221e },
  { XK_logicaland,                  0x2227 },
  { XK_logicalor,                   0x2228 },
  { XK_intersection,                0x2229 },
  { XK_union,                       0x222a },
  { XK_integral,                    0x222b },
  { XK_therefore,                   0x2234 },
  { XK_approximate,                 0x223c },
  { XK_similarequal,                0x2243 },
  { XK_notequal,                    0x2260 },
  { XK_identical,                   0x2261 },
  { XK_lessthanequal,               0x2264 },
  { XK_greaterthanequal,            0x2265 },
  { XK_includedin,                  0x2282 },
  { XK_includes,                    0x2283 },
  { XK_righttack,                   0x22a2 },
  { XK_lefttack,                    0x22a3 },
  { XK_uptack,                      0x22a4 },
  { XK_downtack,                    0x22a5 },
  { XK_upstile,                     0x2308 },
  { XK_downstile,                   0x230a },
  { XK_telephonerecorder,           0x2315 },
  { XK_topintegral,                 0x2320 },
  { XK_botintegral,                 0x2321 },
  { XK_leftanglebracket,            0x2329 },
  { XK_rightanglebracket,           0x232a },
  { XK_quad,                        0x2395 },
  { XK_topleftparens,               0x239b },
  { XK_botleftparens,               0x239d },
  { XK_toprightparens,              0x239e },
  { XK_botrightparens,              0x23a0 },
  { XK_topleftsqbracket,            0x23a1 },
  { XK_botleftsqbracket,            0x23a3 },
  { XK_toprightsqbracket,           0x23a4 },
  { XK_botrightsqbracket,           0x23a6 },
  { XK_leftmiddlecurlybrace,        0x23a8 },
  { XK_rightmiddlecurlybrace,       0x23ac },
  { XK_leftradical,                 0x23b7 },
  { XK_horizlinescan1,              0x23ba },
  { XK_horizlinescan3,              0x23bb },
  { XK_horizlinescan7,              0x23bc },
  { XK_horizlinescan9,              0x23bd },
  { XK_ht,                          0x2409 },
  { XK_lf,                          0x240a },
  { XK_vt,                          0x240b },
  { XK_ff,                          0x240c },
  { XK_cr,                          0x240d },
  { XK_nl,                          0x2424 },
  { XK_horizlinescan5,              0x2500 },
  { XK_vertbar,                     0x2502 },
  { XK_upleftcorner,                0x250c },
  { XK_uprightcorner,               0x2510 },
  { XK_lowleftcorner,               0x2514 },
  { XK_lowrightcorner,              0x2518 },
  { XK_leftt,                       0x251c },
  { XK_rightt,                      0x2524 },
  { XK_topt,                        0x252c },
  { XK_bott,                        0x2534 },
  { XK_crossinglines,               0x253c },
  { XK_checkerboard,                0x2592 },
  { XK_enfilledsqbullet,            0x25aa },
  { XK_enopensquarebullet,          0x25ab },
  { XK_filledrectbullet,            0x25ac },
  { XK_openrectbullet,              0x25ad },
  { XK_emfilledrect,                0x25ae },
  { XK_emopenrectangle,             0x25af },
  { XK_filledtribulletup,           0x25b2 },
  { XK_opentribulletup,             0x25b3 },
  { XK_filledrighttribullet,        0x25b6 },
  { XK_rightopentriangle,           0x25b7 },
  { XK_filledtribulletdown,         0x25bc },
  { XK_opentribulletdown,           0x25bd },
  { XK_filledlefttribullet,         0x25c0 },
  { XK_leftopentriangle,            0x25c1 },
  { XK_soliddiamond,                0x25c6 },
  { XK_emopencircle,                0x25cb },
  { XK_emfilledcircle,              0x25cf },
  { XK_enopencircbullet,            0x25e6 },
  { XK_openstar,                    0x2606 },
  { XK_telephone,                   0x260e },
  { XK_signaturemark,               0x2613 },
  { XK_leftpointer,                 0x261c },
  { XK_rightpointer,                0x261e },
  { XK_femalesymbol,                0x2640 },
  { XK_malesymbol,                  0x2642 },
  { XK_club,                        0x2663 },
  { XK_heart,                       0x2665 },
  { XK_diamond,                     0x2666 },
  { XK_musicalflat,                 0x266d },
  { XK_musicalsharp,                0x266f },
  { XK_checkmark,                   0x2713 },
  { XK_ballotcross,                 0x2717 },
  { XK_latincross,                  0x271d },
  { XK_maltesecross,                0x2720 },
  { XK_kana_comma,                  0x3001 },
  { XK_kana_fullstop,               0x3002 },
  { XK_kana_openingbracket,         0x300c },
  { XK_kana_closingbracket,         0x300d },
  { XK_voicedsound,                 0x309b },
  { XK_semivoicedsound,             0x309c },
  { XK_kana_a,                      0x30a1 },
  { XK_kana_A,                      0x30a2 },
  { XK_kana_i,                      0x30a3 },
  { XK_kana_I,                      0x30a4 },
  { XK_kana_u,                      0x30a5 },
  { XK_kana_U,                      0x30a6 },
  { XK_kana_e,                      0x30a7 },
  { XK_kana_E,                      0x30a8 },
  { XK_kana_o,                      0x30a9 },
  { XK_kana_O,                      0x30aa },
  { XK_kana_KA,                     0x30ab },
  { XK_kana_KI,                     0x30ad },
  { XK_kana_KU,                     0x30af },
  { XK_kana_KE,                     0x30b1 },
  { XK_kana_KO,                     0x30b3 },
  { XK_kana_SA,                     0x30b5 },
  { XK_kana_SHI,                    0x30b7 },
  { XK_kana_SU,                     0x30b9 },
  { XK_kana_SE,                     0x30bb },
  { XK_kana_SO,                     0x30bd },
  { XK_kana_TA,                     0x30bf },
  { XK_kana_CHI,                    0x30c1 },
  { XK_kana_tsu,                    0x30c3 },
  { XK_kana_TSU,                    0x30c4 },
  { XK_kana_TE,                     0x30c6 },
  { XK_kana_TO,                     0x30c8 },
  { XK_kana_NA,                     0x30ca },
  { XK_kana_NI,                     0x30cb },
  { XK_kana_NU,                     0x30cc },
  { XK_kana_NE,                     0x30cd },
  { XK_kana_NO,                     0x30ce },
  { XK_kana_HA,                     0x30cf },
  { XK_kana_HI,                     0x30d2 },
  { XK_kana_FU,                     0x30d5 },
  { XK_kana_HE,                     0x30d8 },
  { XK_kana_HO,                     0x30db },
  { XK_kana_MA,                     0x30de },
  { XK_kana_MI,                     0x30df },
  { XK_kana_MU,                     0x30e0 },
  { XK_kana_ME,                     0x30e1 },
  { XK_kana_MO,                     0x30e2 },
  { XK_kana_ya,                     0x30e3 },
  { XK_kana_YA,                     0x30e4 },
  { XK_kana_yu,                     0x30e5 },
  { XK_kana_YU,                     0x30e6 },
  { XK_kana_yo,                     0x30e7 },
  { XK_kana_YO,                     0x30e8 },
  { XK_kana_RA,                     0x30e9 },
  { XK_kana_RI,                     0x30ea },
  { XK_kana_RU,                     0x30eb },
  { XK_kana_RE,                     0x30ec },
  { XK_kana_RO,                     0x30ed },
  { XK_kana_WA,                     0x30ef },
  { XK_kana_WO,                     0x30f2 },
  { XK_kana_N,                      0x30f3 },
  { XK_kana_conjunctive,            0x30fb },
  { XK_prolongedsound,              0x30fc },
  { XK_Hangul_Kiyeog,               0x3131 },
  { XK_Hangul_SsangKiyeog,          0x3132 },
  { XK_Hangul_KiyeogSios,           0x3133 },
  { XK_Hangul_Nieun,                0x3134 },
  { XK_Hangul_NieunJieuj,           0x3135 },
  { XK_Hangul_NieunHieuh,           0x3136 },
  { XK_Hangul_Dikeud,               0x3137 },
  { XK_Hangul_SsangDikeud,          0x3138 },
  { XK_Hangul_Rieul,                0x3139 },
  { XK_Hangul_RieulKiyeog,          0x313a },
  { XK_Hangul_RieulMieum,           0x313b },
  { XK_Hangul_RieulPieub,           0x313c },
  { XK_Hangul_RieulSios,            0x313d },
  { XK_Hangul_RieulTieut,           0x313e },
  { XK_Hangul_RieulPhieuf,          0x313f },
  { XK_Hangul_RieulHieuh,           0x3140 },
  { XK_Hangul_Mieum,                0x3141 },
  { XK_Hangul_Pieub,                0x3142 },
  { XK_Hangul_SsangPieub,           0x3143 },
  { XK_Hangul_PieubSios,            0x3144 },
  { XK_Hangul_Sios,                 0x3145 },
  { XK_Hangul_SsangSios,            0x3146 },
  { XK_Hangul_Ieung,                0x3147 },
  { XK_Hangul_Jieuj,                0x3148 },
  { XK_Hangul_SsangJieuj,           0x3149 },
  { XK_Hangul_Cieuc,                0x314a },
  { XK_Hangul_Khieuq,               0x314b },
  { XK_Hangul_Tieut,                0x314c },
  { XK_Hangul_Phieuf,               0x314d },
  { XK_Hangul_Hieuh,                0x314e },
  { XK_Hangul_A,                    0x314f },
  { XK_Hangul_AE,                   0x3150 },
  { XK_Hangul_YA,                   0x3151 },
  { XK_Hangul_YAE,                  0x3152 },
  { XK_Hangul_EO,                   0x3153 },
  { XK_Hangul_E,                    0x3154 },
  { XK_Hangul_YEO,                  0x3155 },
  { XK_Hangul_YE,                   0x3156 },
  { XK_Hangul_O,                    0x3157 },
  { XK_Hangul_WA,                   0x3158 },
  { XK_Hangul_WAE,                  0x3159 },
  { XK_Hangul_OE,                   0x315a },
  { XK_Hangul_YO,                   0x315b },
  { XK_Hangul_U,                    0x315c },
  { XK_Hangul_WEO,                  0x315d },
  { XK_Hangul_WE,                   0x315e },
  { XK_Hangul_WI,                   0x315f },
  { XK_Hangul_YU,                   0x3160 },
  { XK_Hangul_EU,                   0x3161 },
  { XK_Hangul_YI,                   0x3162 },
  { XK_Hangul_I,                    0x3163 },
  { XK_Hangul_RieulYeorinHieuh,     0x316d },
  { XK_Hangul_SunkyeongeumMieum,    0x3171 },
  { XK_Hangul_SunkyeongeumPieub,    0x3178 },
  { XK_Hangul_PanSios,              0x317f },
  { XK_Hangul_KkogjiDalrinIeung,    0x3181 },
  { XK_Hangul_SunkyeongeumPhieuf,   0x3184 },
  { XK_Hangul_YeorinHieuh,          0x3186 },
  { XK_Hangul_AraeA,                0x318d },
  { XK_Hangul_AraeAE,               0x318e },
};

bool CompareCodePair(const CodePair& pair, uint32_t unicode) {
  return pair.unicode < unicode;
}

}  // namespace

std::vector<uint32_t> GetKeySymsForUnicode(uint32_t unicode) {
  std::vector<uint32_t> keysyms;

  // Latin-1 characters have the same values in Unicode and KeySym.
  if ((unicode >= 0x0020 && unicode <= 0x007e) ||
      (unicode >= 0x00a0 && unicode <= 0x00ff)) {
    keysyms.push_back(unicode);
  }

  const CodePair* map_end = kKeySymUnicodeMap + base::size(kKeySymUnicodeMap);
  const CodePair* pair =
      std::lower_bound(kKeySymUnicodeMap, map_end, unicode, &CompareCodePair);
  while (pair != map_end && pair->unicode == unicode) {
    keysyms.push_back(pair->keysym);
    ++pair;
  }

  keysyms.push_back(0x01000000 | unicode);
  return keysyms;
}

}  // namespace remoting
