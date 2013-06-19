// Tralics, a LaTeX to XML translator.
// Copyright INRIA/apics/marelle (Jose' Grimm) 2002-2011

// This software is governed by the CeCILL license under French law and
// abiding by the rules of distribution of free software.  You can  use, 
// modify and/ or redistribute the software under the terms of the CeCILL
// license as circulated by CEA, CNRS and INRIA at the following URL
// "http://www.cecill.info". 
// (See the file COPYING in the main directory for details)



#include "tralics.h"
const char* txcommands_rcsid =
  "$Id: txcommands.C,v 2.130 2011/06/28 08:15:25 grimm Exp $";

// This file contains the big switch that interprets (most) commands

extern bool nostraightquotes;
extern bool xkv_patch;

extern  uint leftquote_val;
extern  uint rightquote_val;
extern bool nofloat_hack;

namespace {
  bool seen_document = false;  // did we see \begin{document} ?
  bool quote_started = false;
  TokenList onlypreamble; // token-list allowed only in preamble
  Xmlp open_paren_xml;
  Xmlp close_paren_xml;
  Xmlp the_page_xml;
  Xmlp glo_xml;
}

namespace tcommands {
  name_positions hfill_to_np (subtypes c);
  name_positions vfill_to_np (subtypes c);
}

extern void show_unused_options();

// Initialises the translator. The argument is the name of 
// the document element.
void Parser::init_all(string doc_elt)
{
  seen_document = false;
  quote_started = false;
  onlypreamble.clear();

  open_paren_xml = new Xml(Istring("("));
  close_paren_xml = new Xml(Istring(")"));
  the_page_xml = new Xml(Istring("thepage"),0);
  glo_xml = new Xml(np_glo_name);
  eqtb_int_table[endlinechar_code].val_and_level('\r',level_one);
  eqtb_int_table[newlinechar_code].val_and_level('\n',level_one);
  TL.clear();
  my_stats.set_nb_xboot(the_stack.get_xid().value);
  the_stack.init_all(doc_elt);
  the_stack.add_nl();
  onlypreamble.push_back(hash_table.let_token);
  onlypreamble.push_back(hash_table.atbd_token);
  onlypreamble.push_back(hash_table.notprerr_token);
  

}


// This assumes that the string is plain ASCII, no special XML characters
inline void Parser::process_string (String s)
{
  unprocessed_xml.push_back(s);
}

// This assumes that the string is plain ASCII, no special XML characters
inline void Parser::process_char (int s)
{
  process_char(uint(s));
}


// This is useful for German unlaut. It translates two normal characters.
void Parser::translate_char(uchar c1, uchar c2)
{
  translate_char(CmdChr(letter_catcode,subtypes(c1)));
  translate_char(CmdChr(letter_catcode,subtypes(c2)));
}

// This is used form German escape character, or maybe English or french quotes
// 
void Parser::umlaut()
{
  get_token();
  if(!cur_lang_german()) {
    if(!main_ns::use_quotes) { umlaut_bad(); return; }
    if(cur_cmd_chr.is_letter()) quote_started = true;
    else if (quote_started) quote_started = false;
    else { umlaut_bad(); return; }
    if(!cur_tok.is_invalid()) back_input();
    Token T = Token(other_t_offset,quote_started ? '`' : '\'');
    if(cur_lang_fr())T = Token(other_t_offset,quote_started ? '<' : '>');
    back_input(T);
    back_input(T);
    return;
  }
  if((!cur_cmd_chr.is_letter() && !cur_cmd_chr.is_other())
     || cur_tok.char_val().get_value() >int(nb_characters)) { //
    if(cur_tok.to_cs() == '~'+1) { // "~ -> dash , strange
      translate_char(CmdChr(uchar('-'))); 
      return;
    }
    umlaut_bad();
    return;
  }
  switch(cur_tok.char_val().get_value()) {
  case 'a': translate_char(CmdChr(uchar('\344'))); break;
  case 'o': translate_char(CmdChr(uchar('\366'))); break;
  case 'u': translate_char(CmdChr(uchar('\374'))); break;
  case 'e': translate_char(CmdChr(uchar('\353'))); break;
  case 'i': translate_char(CmdChr(uchar('\357'))); break;
  case 'A': translate_char(CmdChr(uchar('\304'))); break;
  case 'O': translate_char(CmdChr(uchar('\326'))); break;
  case 'U': translate_char(CmdChr(uchar('\334'))); break;
  case 'I': translate_char(CmdChr(uchar('\317'))); break;
  case 'E': translate_char(CmdChr(uchar('\313'))); break;
  case 's': back_input(uclc_list[14]); return;
  case 'z': back_input(uclc_list[14]); return;
  case 'S': back_input(uclc_list[15]); return;
  case 'Z': translate_char('S','Z');  return;
  case 'c': translate_char('c','k');  return;
  case 'C': translate_char('C','K');  return;
  case 'f': translate_char('f','f');  return;
  case 'F': translate_char('F','F');  return;
  case 'l': translate_char('l','l');  return;
  case 'L': translate_char('L','L');  return;
  case 'm': translate_char('m','m');  return;
  case 'M': translate_char('M','M');  return;
  case 'n': translate_char('n','n');  return;
  case 'N': translate_char('N','N');  return;
  case 'p': translate_char('p','p');  return;
  case 'P': translate_char('P','P');  return;
  case 'r': translate_char('r','r');  return;
  case 'R': translate_char('R','R');  return;
  case 't': translate_char('t','t');  return;
  case 'T': translate_char('T','T');  return;
  case '-': translate_char(CmdChr(uchar('-'))); return;
  case '~': translate_char(CmdChr(uchar('-'))); return;
  case '|': return;
  case '"': return;
  case '=': return;
  case '`': LC(); process_string("&#x0201E;"); return;
  case '\'': LC(); process_string("&#x0201D;"); return;
  case '<' : translate_char(CmdChr(uchar(0253))); return;
  case '>' : translate_char(CmdChr(uchar(0273))); return;
  default:
    umlaut_bad();
  }
}

// Case of double-quote plus maybe some other character, result is 
// a double quote
void Parser::umlaut_bad()
{
  if(!cur_tok.is_invalid()) back_input();
  translate_char(CmdChr(letter_catcode,subtypes(uchar('"'))));
}

// This translates a character, that could be a not 8bit character.
// The result might be &#xFFF; or UTF8
void Parser::extended_chars(unsigned int c)
{
  LC();
  if(c<1<<16)
    process_char(c);
  else
    unprocessed_xml.process_big_char(c);
}
  
// Translate current character.
void Parser::translate_char (CmdChr X)
{
  if(!the_stack.in_h_mode()) LC();
  Utf8Char c = X.char_val();
  if(c.is_null()) return; // we do not want null chars in trace or Xml
  if(tracing_commands()) the_log.out_single_char(c);
  if(!c.is_small()) {
    process_char(c);
    return;
  }
  switch(c.get_value()) {
  case '-' :
    minus_sign(X);
    return;
  case '\'':
  case '`':
  case '<' :
  case '>' :
    english_quotes(X);
    return;
  case '"' :
    if(X.is_letter() || InUrlHandler::global_in_url|| 
       InLoadHandler::global_in_load)
      process_char(c);
    else umlaut();
    return;
  case ';':
  case ':' : 
  case '!' : 
  case '?':
  case 0273:
  case 0253:
    french_punctuation(X);
    return;
  default :
    process_char(c);
  }
}


// This translates `'<>
// In some case ``, '', << and >> are translated as \253 and \273
void Parser::english_quotes(CmdChr X)
{
  uchar c = X.char_val().get_value(); // Should be a small int
  if(InUrlHandler::global_in_url 
     || InLoadHandler::global_in_load) {
    if(c=='<') process_string("&lt;"); 
    else if(c=='>') process_string("&gt;");
    else unprocessed_xml.push_back(c); 
  } else {
    if(X.is_other() && (c=='\'' || c=='`' || cur_lang_fr())) {
      get_x_token(); 
      if(cur_cmd_chr.is_other() && cur_cmd_chr.char_val() == uint(c)) {
	if(c=='\'') process_char(0x201d); 
	else if(c=='`') process_char(0x201c);
	else if(c=='<') french_punctuation(CmdChr(uchar('\253')));
	else  french_punctuation(CmdChr(uchar('\273')));
	return;
      }
      if(!cur_tok.is_invalid())  back_input();
    }
    if(c=='<') process_string("&lt;"); 
    else if(c=='>') process_string("&gt;"); 
    else if(c=='\'' && X.is_other()) 
      process_char(rightquote_val); 
    else if(c=='`' && X.is_other()) 
      process_char(leftquote_val);
    else unprocessed_xml.push_back(c); 
  }
  if(X.is_letter() && ! the_main->get_zws_mode())
    process_string(the_main->get_zws_elt() ?  "&#x200B;" :"<zws/>");
}

// This translates -, --, or ---.
void Parser::minus_sign(CmdChr X)
{
  if(InUrlHandler::global_in_url || InLoadHandler::global_in_load)
    process_char('-');
  else if(X.is_letter()) {
    process_char('-');
    if(! the_main->get_zws_mode())
      process_string(the_main->get_zws_elt() ?  "&#x200B;" :"<zws/>");
  } else {
    get_x_token();
    if(cur_cmd_chr.is_minus_sign()) {
      get_x_token();
      if(cur_cmd_chr.is_minus_sign()) {
	process_char(0x2014);
      } else {
	if(!cur_tok.is_invalid()) back_input();
	process_char(0x2013);
      }
    } else {
      if(!cur_tok.is_invalid()) back_input();
      process_char('-');
    }
  }
}

// This handles :;!?\253 \273. Especially in French.
void Parser::french_punctuation(CmdChr X)
{
  uchar c = X.char_val().get_value();
  if(InUrlHandler::global_in_url || InLoadHandler::global_in_load 
     || X.is_letter() || !cur_lang_fr()) {
    extended_chars(c);
    return;
  }
  if(c== 0xAB) {
    for(;;) {
      get_x_token();
      if(cur_cmd_chr.is_space()) continue;
      if(cur_cmd_chr.get_cmd()==cst_cmd && 
	 cur_cmd_chr.get_chr()==nobreakspace_code) break;
      if(cur_cmd_chr.get_cmd()==cst1_cmd &&
	 cur_cmd_chr.get_chr()==comma_code) break;
      if(!cur_tok.is_invalid()) back_input();
      break;
    }
    extended_chars(c);
    process_char(0xA0);
    return;
  }
  unprocessed_xml.remove_last_space();
  process_char(0xA0);
  extended_chars(c);
} 

void Parser::T_cst2(int c)
{
  LC();
  if(c==numero_code) process_string("n");
  if(c==Numero_code) process_string("N");
  flush_buffer();
  {
    Xmlp res = the_stack.fonts1(np_s_sup);
    String s = "o";
    if(c==ier_code) s = "er";
    else if(c==iers_code) s = "ers";
    else if(c==iere_code) s = "re";
    else if(c==ieres_code) s = "res";
    else if(c==ieme_code) s = "e";
    else if(c==iemes_code) s = "es"; 
    res ->push_back(new Xml(Istring(s)));
    the_stack.add_last(res);
  }
  T_xspace();
}


void Parser::T_cst1(int c)
{
  switch(c) {
  case underscore_code : process_char('_'); return;;
  case sharp_code : process_char('#'); return;
  case dash_code : return; // \- is: \discretionary{-}{}{}
  case slash_code :  // \slash is /\penalty \exhyphenpenalty 
    process_char('/'); return;
  case textbackslash_code : process_char('\\'); return;
  case null_code : return; // unused
  case LaTeX_code : process_string("<LaTeX/>"); return;
  case TeX_code : process_string("<TeX/>"); return;;
  case SS_code : process_string("SS"); return;
  case dots_code : process_string("..."); return;
  case ldots_code : process_string("..."); return;
  case space_code : process_string(" "); return;
  case dollar_code : process_char('$'); return;
  case percent_code : process_char('%'); return;
  case exclam_code : return;
  case lbrace_chr : process_char('{'); return;
  case rbrace_chr : process_char('}'); return;
    // case textasciigrave_code : process_char(0x2035); return;
    // case textasciicircum_code : process_char(0x2303); return;
    //  case textasciiacutey_code : process_char(0x2019); return;
  case textasciiacutex_code : process_char(0x2032); return;
  case textbullet_code : process_char(0x2022); return;
  case tdagger_code : process_char(0x2020); return;
  case tddagger_code : process_char(0x2021); return;
    // case textnospace_code : process_char(0x200B); return;
  case ss_code : process_char(0xDF); return;
  case i_code : process_char(0x131); return;
  case msharp_code : process_char(0x266F); return;
  case natural_code : process_char(0x266E); return;
  case flat_code :  process_char(0x266D); return;
  case quad_code :  
    process_char(0xA0);
    process_char(0xA0);
    process_char(0xA0);
    return;
  case qquad_code :
    process_char(0xA0);
    process_char(0xA0);
    process_char(0xA0);
    process_char(0xA0);
    process_char(0xA0);
    process_char(0xA0);
    return;
  case amp_code : process_string("&amp;"); return;
  case comma_code :  process_char(0xA0); return;
  case nbsp_code :  process_char(0xA0); return;
  case j_code : process_char('j'); return;
    // case textvisiblespace_code :  process_char(0x2423); return;
  case guillemet_og_code : // opening guillemet
    process_char(0xAB);
    process_char(0xA0);
    remove_initial_space_and_back_input();
    return;
  case guillemet_fg_code : // closing guillemet
    unprocessed_xml.remove_last_space();
    process_char(0xA0);
    process_char(0xBB); 
    T_xspace();
    return;
    //  case emdash_code : process_char(0x2014); return;
    // case endash_code : process_char(0x2013); return;
  default: process_string("<Unknownelement/>"); return;
  }
}


// This translates \ding{N}, a reference to Zapf Dingbats
// In latex, this calls \char. Here we call scan_braced_int
void Parser::T_ding()
{
  Token T = cur_tok;
  int c = scan_braced_int(T);
  int r =0;
  switch(c) {
  case 33:  r =0x2701; break;
  case 34:  r =0x2702; break;
  case 35:  r =0x2703; break;
  case 36:  r =0x2704; break;
  case 37:  r =0x260E; break; // G
  case 38:  r =0x2706; break;
  case 39:  r =0x2707; break;
  case 40:  r =0x2708; break;
  case 41:  r =0x2709; break;
  case 42:  r =0x261B; break; //G
  case 43:  r =0x261E; break; // G
  case 44:  r =0x270C; break;
  case 45:  r =0x270D; break;
  case 46:  r =0x270E; break;
  case 47:  r =0x270F; break;
  case 48: r = 0x2710; break;
  case 49: r = 0x2711; break;
  case 50: r = 0x2712; break;
  case 51: r = 0x2713; break;
  case 52: r = 0x2714; break;
  case 53: r = 0x2715; break;
  case 54: r = 0x2716; break;
  case 55: r = 0x2717; break;
  case 56: r = 0x2718; break;
  case 57: r = 0x2719; break;
  case 58: r = 0x271A; break;
  case 59: r = 0x271B; break;
  case 60: r = 0x271C; break;
  case 61: r = 0x271D; break;
  case 62: r = 0x271E; break;
  case 63: r = 0x271F; break;
  case 64: r = 0x2720; break;
  case 65: r = 0x2721; break;
  case 66: r = 0x2722; break;
  case 67: r = 0x2723; break;
  case 68: r = 0x2724; break;
  case 69: r = 0x2725; break;
  case 70: r = 0x2726; break;
  case 71: r = 0x2727; break;
  case 72: r = 0x2605; break; //G 
  case 73: r = 0x2729; break;
  case 74: r = 0x272A; break;
  case 75: r = 0x272B; break;
  case 76: r = 0x272C; break;
  case 77: r = 0x272D; break;
  case 78: r = 0x272E; break;
  case 79: r = 0x272F; break;
  case 80: r = 0x2730; break;
  case 81: r = 0x2731; break;
  case 82: r = 0x2732; break;
  case 83: r = 0x2733; break;
  case 84: r = 0x2734; break;
  case 85: r = 0x2735; break;
  case 86: r = 0x2736; break;
  case 87: r = 0x2737; break;
  case 88: r = 0x2738; break;
  case 89: r = 0x2739; break;
  case 90: r = 0x273A; break;
  case 91: r = 0x273B; break;
  case 92: r = 0x273C; break;
  case 93: r = 0x273D; break;
  case 94: r = 0x273E; break;
  case 95: r = 0x273F; break;
  case 96: r = 0x2740; break;
  case 97: r = 0x2741; break;
  case 98: r = 0x2742; break;
  case 99: r = 0x2743; break;
  case 100: r = 0x2744; break;
  case 101: r = 0x2745; break;
  case 102: r = 0x2746; break;
  case 103: r = 0x2747; break;
  case 104: r = 0x2748; break;
  case 105: r = 0x2749; break;
  case 106: r = 0x274A; break;
  case 107: r = 0x274B; break;
  case 108: r = 0x25CF; break; // G
  case 109: r = 0x274D; break;
  case 110: r = 0x25A0; break; // G
  case 111: r = 0x274F; break;
  case 112: r = 0x2750; break;
  case 113: r = 0x2751; break;
  case 114: r = 0x2752; break;
  case 115: r = 0x25B2; break; //g
  case 116: r = 0x25BC; break;  // g
  case 117: r = 0x25C6; break;  // g
  case 118: r = 0x2756; break;
  case 119: r = 0x25D7; break;
  case 120: r = 0x2758; break;
  case 121: r = 0x2759; break;
  case 122: r = 0x275A; break;
  case 123: r = 0x275B; break;
  case 124: r = 0x275C; break;
  case 125: r = 0x275D; break;
  case 126: r = 0x275E; break;
  case 161: r = 0x2761; break;
  case 162: r = 0x2762; break;
  case 163: r = 0x2763; break;
  case 164: r = 0x2764; break;
  case 165: r = 0x2765; break;
  case 166: r = 0x2766; break;
  case 167: r = 0x2767; break;
  case 168: r = 0x2663; break; // g
  case 169: r = 0x2666; break; //
  case 170: r = 0x2665; break; //
  case 171: r = 0x2660; break; //
  case 172: r = 0x2460; break;
  case 173: r = 0x2461; break;
  case 174: r = 0x2462; break;
  case 175: r = 0x2463; break;
  case 176: r = 0x2464; break;
  case 177: r = 0x2465; break;
  case 178: r = 0x2466; break;
  case 179: r = 0x2467; break;
  case 180: r = 0x2468; break;
  case 181: r = 0x2469; break;
  case 182: r = 0x2776; break;
  case 183: r = 0x2777; break;
  case 184: r = 0x2778; break;
  case 185: r = 0x2779; break;
  case 186: r = 0x277A; break;
  case 187: r = 0x277B; break;
  case 188: r = 0x277C; break;
  case 189: r = 0x277D; break;
  case 190: r = 0x277E; break;
  case 191: r = 0x277F; break;
  case 192: r = 0x2780; break;
  case 193: r = 0x2781; break;
  case 194: r = 0x2782; break;
  case 195: r = 0x2783; break;
  case 196: r = 0x2784; break;
  case 197: r = 0x2785; break;
  case 198: r = 0x2786; break;
  case 199: r = 0x2787; break;
  case 200: r = 0x2788; break;
  case 201: r = 0x2789; break;
  case 202: r = 0x278A; break;
  case 203: r = 0x278B; break;
  case 204: r = 0x278C; break;
  case 205: r = 0x278D; break;
  case 206: r = 0x278E; break;
  case 207: r = 0x278F; break;
  case 208: r = 0x2790; break;
  case 209: r = 0x2791; break;
  case 210: r = 0x2792; break;
  case 211: r = 0x2793; break;
  case 212: r = 0x2794; break;
  case 213: r = 0x2192; break;
  case 214: r = 0x2194; break;
  case 215: r = 0x2195; break;
  case 216: r = 0x2798; break;
  case 217: r = 0x2799; break;
  case 218: r = 0x279A; break;
  case 219: r = 0x279B; break;
  case 220: r = 0x279C; break;
  case 221: r = 0x279D; break;
  case 222: r = 0x279E; break;
  case 223: r = 0x279F; break;
  case 224: r = 0x27A0; break;
  case 225: r = 0x27A1; break;
  case 226: r = 0x27A2; break;
  case 227: r = 0x27A3; break;
  case 228: r = 0x27A4; break;
  case 229: r = 0x27A5; break;
  case 230: r = 0x27A6; break;
  case 231: r = 0x27A7; break;
  case 232: r = 0x27A8; break;
  case 233: r = 0x27A9; break;
  case 234: r = 0x27AA; break;
  case 235: r = 0x27AB; break;
  case 236: r = 0x27AC; break;
  case 237: r = 0x27AD; break;
  case 238: r = 0x27AE; break;
  case 239: r = 0x27AF; break;
  case 241: r = 0x27B1; break;
  case 242: r = 0x27B2; break;
  case 243: r = 0x27B3; break;
  case 244: r = 0x27B4; break;
  case 245: r = 0x27B5; break;
  case 246: r = 0x27B6; break;
  case 247: r = 0x27B7; break;
  case 248: r = 0x27B8; break;
  case 249: r = 0x27B9; break;
  case 250: r = 0x27BA; break;
  case 251: r = 0x27BB; break;
  case 252: r = 0x27BC; break;
  case 253: r = 0x27BD; break;
  case 254: r = 0x27BE; break;
  }
  LC();
  if(!r) { process_string("bad!"); return; }
  process_char((r));  
}


// \begin{minipage}
void Parser::T_minipage()
{
  Token T = cur_tok;
  Istring pos = the_names[get_ctb_opt(false)];
  ignore_next_optarg();
  Istring ipos = the_names[get_ctb_opt(true)];
  ignore_next_optarg(); // really needed ?
  scan_glue(it_glue,T,false);
  Istring w =  the_main->SH.find_scaled( ScaledInt(cur_val.get_glue_width()));
  if(the_stack.in_v_mode()) leave_v_mode();
  the_stack.push1(np_minipage);
  the_stack.set_v_mode();
  state=state_S;
  the_stack.add_att_to_last(the_names[np_minipage_width],w);
  the_stack.add_att_to_last(the_names[np_pos],pos);
  the_stack.add_att_to_last(the_names[np_posi],ipos);
  word_define(incentering_code,0,false);
}

// \begin{xmlelement}
void Parser::T_xmlenv(subtypes c) 
{ 
  flush_buffer();
  string a = sT_next_arg();
  if(c==0 && the_stack.in_v_mode()) {
    leave_v_mode();
  } else if(c==1) leave_h_mode();
  else if(c==2) {
    if(the_stack.in_v_mode()) leave_v_mode();
  }
  the_stack.push(the_names[cst_elt],new Xml(Istring(a),0));
  if(c==2) the_stack.set_v_mode();
  remove_initial_space_and_back_input();
}

// \end{xmlelement}
void Parser::T_xmlenv_end(subtypes c)
{
  flush_buffer();
  if(c==1)
    leave_h_mode();
  else if(c==2) {}
  the_stack.pop_if_frame(the_names[cst_p]);
  the_stack.pop(cst_elt);
}

// \end{itemize},  \end{description}, \end{enumerate}, 
void Parser::T_listenv_end(symcodes x)
{
  leave_h_mode();
  the_stack.pop_if_frame(the_names[np_item]);
  the_stack.pop(np_list);
  the_stack.add_nl();
}

void Parser::T_atdocument(subtypes c)
{
  TokenList L = mac_arg();
  if(c==0) document_hook.splice(document_hook.end(),L);
  else end_document_hook.splice(end_document_hook.end(),L);
}


/// Translates \begin{glossaire}
void Parser::T_glossaire()
{
  leave_h_mode();
  the_stack.push1(the_names[np_gloss],np_list);
  the_stack.add_att_to_last(np_type,np_gloss);
  the_stack.add_last(new Xml(np_head,glo_xml));
  the_stack.set_no_mode();
}

/// Translates \end{glossaire}
void Parser::T_glossaire_end()
{
  int n = the_stack.top_stack()->size();
  the_stack.pop(np_gloss);
  if(n==1) parse_error("empty glossaire");
}

// case \begin{figure}\begin{table}
// c=� is wrapfigure
void Parser::T_figure_table(symcodes x, subtypes c)
{
  Istring opt = nT_next_optarg();
  Istring place, overhang,width;
  if(c==2) {
    place = nT_next_arg();
    overhang = nT_next_optarg();
    width = nT_next_arg();
  }
  word_define(incentering_code,1,false);
  leave_h_mode();
  the_stack.push1(x==figure_cmd ?np_float_figure: np_float_table);
  if(c==2) {
    if(!opt.empty()) the_stack.add_att_to_last(Istring("narrow"),opt);
    the_stack.add_att_to_last(np_place,place);
    if(!overhang.empty()) the_stack.add_att_to_last(Istring("overhang"),overhang);
    the_stack.add_att_to_last(np_width,width);
  } else {
    if(!opt.empty()) the_stack.add_att_to_last(np_place,opt);
    if(c==1) the_stack.add_att_to_last(np_starred,np_true);
  }
  refstepcounter(x==figure_cmd ?"figure":"table",true);
  the_stack.set_v_mode(); 
}

// Translates \enf{figure} or \end{table}
void Parser::T_figure_table_end(bool is_fig)
{
  Istring name = the_names[is_fig ? np_float_figure : np_float_table];
  leave_h_mode();
  Xmlp aux = the_stack.top_stack();
  if(!aux->has_name(name)) 
    parse_error("no figure/table on stack");
  else if(!nofloat_hack)
    aux->postprocess_fig_table(is_fig);
  the_stack.pop(name);
  the_stack.add_nl();
}


// case \end{document}
void Parser::T_enddocument(subtypes c)
{
  if(c==0) {
    x_input(end_all_input_code);
    hash_table.eval_let("AtEndDocument","@firstofone");
    back_input(hash_table.real_end_token);
    back_input(end_document_hook);
  } else {
    flush_buffer();
    the_stack.end_module();
  }
}

// case \begin{document}
void Parser::T_begindocument()
{
  if(seen_document) {
    parse_error("Two environments named document");
    return;
  }
  if(cur_level != 2)
    parse_error("\\begin{document} not at level 0");
  seen_document = true;
  if(!tralics_ns::titlepage_is_valid()) add_language_att();
  pop_level(true,bt_env);
  cur_level = level_one; // this is the outer level...
  if(the_main->d_verbose()) tracing_all();
  if(tracing_commands()) 
    the_log << lg_startstack << "level set to 1" << lg_end;
  tralics_ns::bibtex_bootagain();
  hash_table.eval_let("AtBeginDocument","@firstofone");
  {
    Buffer& b = mac_buffer;
    TokenList L;
    b.reset();
    b.push_back("\\let\\do\\noexpand\\ignorespaces\n");
    add_buffer_to_list(b,L,"(AtBeginDocument hook)");
    back_input(L);
  } 
  back_input(onlypreamble);
  back_input(document_hook);
  show_unused_options();
  if(tracing_commands()) the_log << "atbegindocumenthook= " << TL << "\n";
} 

// case \begin \end
void Parser::T_beginend(symcodes x)
{
  flush_buffer();
  bool begin = x==begin_cmd;
  string S = get_env();
  if(tracing_commands())
    the_log << lg_startbracebs  << (begin ? "begin " : "end ") << S 
	    << lg_endbrace;
  if(begin) T_begin(S); else T_end(S);
}

// Case of \@setmode=12
void Parser::T_setmode()
{
  scan_optional_equals();
  int n = scan_int(cur_tok);
  if(!(n>=0 && n<=6)) n =0;
  the_stack.set_mode(mode(n));
  return;
}

// Translates \aftergroup\token
void Parser::T_aftergroup()
{
  get_token();  
  SaveAux *y = new SaveAuxAftergroup(cur_tok);
  push_save_stack(y);
}

// Translates \hskip1em, \vskip1em or \mskip3mu
void Parser::T_scan_glue(subtypes c)
{
  if(c==mskip_code) {
    scan_glue(it_mu,cur_tok); 
    // ignore value...
    return;
  } else {
    scan_glue(it_glue,cur_tok);
    // not perfect...
    append_glue(ScaledInt(cur_val.get_glue_width()),c==vskip_code);
  }
}

// Translates \bauthors{...} or \beditors{...}
void Parser::T_bauteursediteurs(subtypes c)
{
  mode m = the_stack.get_mode();
  need_bib_mode();
  flush_buffer();
  T_arg1(c ==0 ? np_bauteurs : np_bediteurs);
  the_stack.set_mode(m);
  the_stack.add_nl();
}

void Parser::T_anchor() 
{
  static int anchor_id = 0;
  anchor_id++;
  string s = sT_next_optarg();
  if(s.empty()) {
    mac_buffer << bf_reset << anchor_id;
    s = mac_buffer.to_string();
  }
  the_stack.add_anchor(s);  
  mac_buffer << bf_reset << "aid" << anchor_id;
  add_id(mac_buffer.to_string());
}


// Translates \unhbox, \unhcopy \unvbox \unvcopy
void Parser::T_un_box(subtypes c)
{
  int i = scan_eight_bit_int();
  if(c==unhbox_code) leave_v_mode();
  Xmlp cur_box = box_table[i].get_val();
  the_stack.unbox(cur_box);
  if(c==unhbox_code || c==unvbox_code)
    box_table[i].set_val(0);
  return; // else does nothing
}

name_positions tcommands::hfill_to_np (subtypes c)
{
  if(c==hfill_code) return np_hfill;
  if(c==hfilneg_code) return np_hfilneg;
  if(c==hss_code) return np_hss;
  return np_hfil;
}

name_positions tcommands::vfill_to_np (subtypes c)
{
  if(c==vfill_code) return np_vfill;
  if(c==vfilneg_code) return np_vfilneg;
  if(c==vss_code) return np_vss;
  return np_vfil;
}


// Translates a command.
void Parser::translate01()
{
  Token T=name_for_error;
  name_for_error = cur_tok;
  translate03();
  name_for_error = T;
}

void Parser::translate03()
{
  if(cur_cmd_chr.get_cmd()==underscore_catcode && 
     InLoadHandler::global_in_load) {
    translate_char(cur_cmd_chr);
    return;
  }
  subtypes c = cur_cmd_chr.get_chr();
  symcodes x = cur_cmd_chr.get_cmd();
  switch(x) {
  case cst1_cmd:
  case cst_cmd:
    LC();
    T_cst1(c);
    return;
  case cst2_cmd:
    T_cst2(c);
    return;
  case nobreakspace_cmd: 
    LC();
    if(InUrlHandler::global_in_url) process_char('~');
    else process_char(0xA0);
    return;
  case leave_v_mode_cmd:
    leave_v_mode();
    return;
  case space_catcode:
    if(the_stack.in_v_mode() || the_stack.in_no_mode() || the_stack.in_bib_mode())
      return; 
    process_char(uint(c));
    return;
  case letter_catcode:
  case other_catcode:
    translate_char(cur_cmd_chr);
    return;
  case char_num_cmd:
    extended_chars(scan_27bit_int());
    return;
  case char_given_cmd:
    extended_chars(c);
    return;
  case fvset_cmd:
    special_fvset();
    return;
  case biblio_cmd:
    T_biblio();
    return;
  case bibliographystyle_cmd:
    T_bibliostyle();
    return;
  case insertbibliohere_cmd:
    add_bib_marker(true);
    return;
  case inhibit_xml_cmd:
    the_main->inhibit_xml();
    main_ns::log_and_tty << "\nsyntaxonly: no XML file will be produced\n";
    return;
  case endcsname_cmd:
    parse_error("Extra \\endcsname");
    return;
  case xmllatex_cmd:
    LC();
    unprocessed_xml << xmllatex();
    return;
  case scan_glue_cmd: 
    T_scan_glue(c);
    return;
  case kern_cmd:
    scan_dimen(c==1,cur_tok);
    // so what ? we could append hspace or vspace here.
    return;
  case aparaitre_cmd:
    LC();
    // FIXME this is latex not xml
    if(eqtb_int_table[language_code].get_val() ==1) 
      process_string("\\`a para\\^itre");
    else process_string("to appear");
    return;
  case calc_cmd:
    exec_calc();
    return;
  case 3: 
    flush_buffer(); 
    T_math(-1);
    return;
  case 4:
    T_ampersand();
    return;
  case open_catcode:
    push_level(bt_brace);
    return;
  case close_catcode:
    pop_level(false,bt_brace);
    return;
  case begingroup_cmd:
    flush_buffer();
    if(c==0) push_level(bt_semisimple);
    else if(c==1) pop_level(false,bt_semisimple);
    else pop_level(true,bt_env);
    return;
  case hat_catcode: 
  case underscore_catcode: 
    if(InLoadHandler::global_in_load||is_pos_par(nomath_code))
      translate_char(CmdChr(letter_catcode,c));
    else parse_error("Missing dollar not inserted, token ignored: ",
		     cur_tok.tok_to_str(),"Missing dollar");
    return;
  case backslash_cmd: 
    if(c==0) 
      T_backslash();
    else
      T_newline();
    return;
  case hline_cmd:  // cline, hline
    if(c) T_cline(); 
    else {
      need_array_mode();
      if(the_stack.in_array_mode()) the_stack.T_hline();
    }
    return;
  case skip_cmd: {
    int w =  c==smallskip_code ? 3 : c == medskip_code ? 6 : 12;
    append_glue(w<<16, true);
    return;
  }
  case hfill_cmd:
    leave_v_mode();
    the_stack.add_newid0(tcommands::hfill_to_np(c));
    return;
  case vfill_cmd: 
    leave_h_mode();
    the_stack.add_newid0(tcommands::vfill_to_np(c));
    return;
  case sub_cmd:
  case soul_cmd:
    T_fonts(name_positions(np_s_sup+c));
    return;
  case trees_cmd:
    T_trees(c);
    return;
  case matter_cmd:
    T_matter(c);
    return;
  case arg_font_cmd:
    T_fonts(np_font_sc);
    return;
  case special_math_cmd:
    if(c==overline_code || c==underline_code)
      T_fonts(c==overline_code ? np_overline : np_underline);
    else math_only();
    return;
  case noargfont_cmd:
    see_font_change(c);
    return;
  case argfont_cmd:
    arg_font(c);
    return;
  case oldfont_cmd:
    old_font();
    return;
  case fontsize_cmd:
    translate_font_size();
    return;
  case fnhack_cmd:  fnhack(); return;
  case interpret_rc_cmd:  interpret_rc(); return;
  case footnote_cmd: 
    T_cap_or_note(false);
    return;
  case selectfont_cmd:
    font_has_changed();
    return;
  case ltfont_cmd:
    flush_buffer();
    cur_font.ltfont(sT_next_arg(),c);
    return;
  case usefont_cmd:
    T_usefont();
    return;
  case isin_cmd:
    T_isin();
    return;
  case citation_cmd:
    // should be mode independent...
    T_citation(); 
    the_stack.add_nl();
    return;
  case bib_cmd: 
    T_bauteursediteurs(c);
    return;
  case line_cmd: 
    T_line(c); 
    return;
  case bpers_cmd: 
    T_bpers(); 
    return;
  case cititem_cmd: 
    T_cititem();
    return;
  case bibitem_cmd:
    c==1 ? T_empty_bibitem() : T_bibitem();
    return;
  case end_citation_cmd : 
    the_stack.pop(np_citation);
    return;
  case ignoreA_cmd:
    T_ignoreA();
    return;
        // these are mode independent...
  case ignore_cmd :
    if(c==addnl_code)  { flush_buffer(); the_stack.add_nl(); }
    else if(c==unskip_code) {
      if(unprocessed_xml.empty()) the_stack.remove_last_space();
      else unprocessed_xml.remove_last_space();
    }
    return;
  case relax_cmd:
    return;
  case nolinebreak_cmd:
    ignore_next_optarg();
    return;
  case ignore_one_argument_cmd:
    if(c==patterns_code || c==hyphenation_code || c==special_code) 
      scan_left_brace_and_bi();
    ignore_next_arg();
    return;
  case ignore_two_argument_cmd:
    ignore_next_arg();
    ignore_next_arg();
    return;
  case defineverbatimenv_cmd:
    T_define_verbatim_env();
    return;
  case saveverb_cmd:
    T_save_use_verb(true);
    return;
  case xkeyval_cmd: T_xkeyval(c); return;
  case add_to_macro_cmd:
    T_addtomacro(c==1);
    return;
  case makeatletter_cmd:
    word_define('@',letter_catcode,false);
    return;
  case makeatother_cmd:
    word_define('@',other_catcode,false);
    return;
  case numberedverbatim_cmd:
    numbered_verbatim = true;
    return;
  case unnumberedverbatim_cmd:
    numbered_verbatim = false;
    return;
  case after_assignment_cmd:
    get_token();
    set_after_ass_tok(cur_tok);
    if(tracing_commands())
      the_log << lg_startbracebs << "afterassignment: " << cur_tok
	      << lg_endbrace;
    return;
  case last_item_cmd: 
    parse_error("Read only variable ", cur_tok, "","readonly");
    return;
  case un_box_cmd: 
    T_un_box(c);
    return;
  case extension_cmd: // 
    tex_extension(c);
    return;
  case setlanguage_cmd: //  strange...
    scan_int(cur_tok);
    return;
  case xray_cmd: 
    xray(c);
    return;
  case move_cmd:
    scan_dimen(false,cur_tok); // ignore dimension
    scan_box(move_location); // read a box and insert the value
    return;
  case save_box_cmd:
    T_save_box(c==0);
    return;
  case make_box_cmd:
    begin_box(makebox_location,c);
    return;
  case leader_ship_cmd:
    scan_box(c==shipout_code ? shipout_location: c==leaders_code ? leaders_location:
	     c== cleaders_code ? cleaders_location: xleaders_location);
    return;
  case tracingall_cmd:
    tracing_all();
    return;
  case ifstar_cmd:
    if_star();
    return;
  case vglue_cmd:
    if(c==0) T_par1(); else leave_v_mode();
    T_scan_glue(c==0? vskip_code : hskip_code);
    return;
  case for_cmd: xkv_for(c); return;
  case ifnextchar_cmd:
    if_nextchar(c==0);
    return;
  case typeout_cmd:
    typeout(c);
    return;
  case newif_cmd:
    newif();
    return;
  case newcount_cmd:
    define_new_something(c);
    return;
  case newboolean_cmd:
    new_boolean(c);
    return;
  case setboolean_cmd:
    set_boolean();
    return;
  case ifthenelse_cmd:
    ifthenelse();
    return;
  case whiledo_cmd:
    while_do();
    return;
  case setmode_cmd: 
    T_setmode();
    return;
  case toks_register_cmd:
  case assign_toks_cmd:
  case assign_int_cmd:
  case assign_dimen_cmd:
  case assign_glue_cmd:
  case assign_mu_glue_cmd:
  case assign_font_dimen_cmd:
  case assign_font_int_cmd:
  case assign_enc_char_cmd:
  case set_aux_cmd:
  case set_prev_graf_cmd:
  case set_page_dimen_cmd:
  case set_page_int_cmd:
  case set_box_dimen_cmd:
  case set_shape_cmd:
  case def_code_cmd:
  case def_family_cmd:
  case set_font_cmd:
  case set_mathprop_cmd:
  case set_mathchar_cmd:
  case def_font_cmd:
  case register_cmd:
  case advance_cmd:
  case multiply_cmd:
  case divide_cmd:
  case prefix_cmd:
  case let_cmd:
  case shorthand_def_cmd:
  case read_to_cs_cmd:
  case def_cmd:
  case set_box_cmd:
  case set_interaction_cmd:
    prefixed_command();
    return;
  case shortverb_cmd: 
    short_verb(c);
    return;
  case usecounter_cmd: { 
    Token T = cur_tok;
    Buffer b;
    TokenList L0 = mac_arg();
    if(!csname_aux("c@","",L0,false,b)) {
      bad_counter0();
      return;
    }
    if(!counter_check(b,false,T)) return;
    T_use_counter(b.to_string(2));
    return;
  }
  case newcounter_cmd: 
    counter(true);
    return;
  case fp_cmd:
  case fpif_cmd:
    exec_fp_cmd(c);
    return;
  case fpi_cmd:
    exec_fpi_cmd(c);
    return;
  case aftergroup_cmd: 
    T_aftergroup();
    return;
  case listfiles_cmd:
    list_files_p = true;
    return;
  case ignorep_cmd :
  case par_cmd: 
    T_par1();
    return;
  case start_par_cmd:
    implicit_par(c); 
    return;
  case caption_cmd: 
    T_cap_or_note(true);
    return;
  case cite_cmd: T_cite(c); return;
  case item_cmd : 
    T_item(c); 
    return;
  case doc_class_cmd : 
    T_documentclass(!the_stack.in_v_mode() || seen_document);
    return;
  case titlepage_cmd : 
    if(!the_stack.in_v_mode()) wrong_mode("Bad titlepage command");
    T_titlepage(c);
    return;
  case package_cmd: 
    if(!the_stack.in_v_mode() || seen_document)
      wrong_mode("Bad \\usepackage command");
    T_usepackage();
    return;
  case if_package_loaded_cmd:
    T_if_package_loaded(c==0);
    return;
  case if_package_later_cmd:
    T_if_package_later(c==0);
    return;
  case if_package_with_cmd:
    T_if_package_with(c==0);
    return;
  case provides_package_cmd:
    T_provides_package(c==0);
    return;
  case load_with_options_cmd:
    T_load_with_options(c==0);
    return;
  case inputclass_cmd:
    T_inputclass();
    return;
  case expandtwoargs_cmd: expand_twoargs(); return;
  case removeelement_cmd: T_remove_element(); return;
  case pass_options_cmd:
    if(xkv_patch)
      xkv_pass_options(c==0);
    else
      T_pass_options(c==0);
    return;
  case declare_options_cmd:
    T_declare_options();
    return;
  case options_not_used_cmd:
    T_option_not_used();
    return;
  case process_options_cmd:
    T_process_options();
    return;
  case at_end_of_class_cmd:
    T_at_end_of_class();
    return;
  case execute_options_cmd:
    T_execute_options();
    return;
  case needs_format_cmd:
    ignore_next_arg();
    ignore_next_optarg();
    return;
  case subfigure_cmd: 
    T_subfigure();
    return;
  case pop_stack_cmd: 
    pop_all_levels();
    return;
  case section_cmd:
    T_paras(c);
    return;
  case label_cmd: 
    flush_buffer();
    if(c==1) T_anchor();
    else T_label();
    return;
  case ref_cmd: 
    leave_v_mode();
    T_ref(c==0);
    return;
  case hspace_cmd: 
    T_hspace(c);
    return;
  case eqref_cmd: // Case \XMLref
    {
      int n = read_elt_id(cur_tok);
      string a = sT_next_arg();
      Xid(n).add_ref(a);
    }
    return;
  case box_cmd:
    T_mbox(c); 
    return;
  case centering_cmd:
    word_define(incentering_code,c,false);
    if(c) the_stack.add_center_to_p();
    return;
  case fbox_cmd: 
    if(c==dashbox_code) T_fbox_dash_box();
    else if(c==rotatebox_code) T_fbox_rotate_box();
    else T_fbox(c); 
    return;
  case includegraphics_cmd:
    includegraphics(c);
    return;
  case fancy_cmd:
    T_fancy();
    return;
  case xfancy_cmd:
    T_xfancy();
    return;
  case xthepage_cmd:
    flush_buffer();
    the_stack.add_last(the_page_xml);
    return;
  case case_shift_cmd:
    change_case(c);
    return;
  case linebreak_cmd:
    ignore_next_optarg();
    return;
  case url_cmd: 
    T_url(c);
    return;
  case hanl_cmd: 
    T_hanl(c);
    return;
  case error_cmd: 
    T_error();
    return;
  case glo_cmd: 
    T_glo();
    return;
  case reevaluate_cmd: 
    T_reevaluate();
    return;
  case xmlelt_cmd:
    T_xmlelt(c);
    return;
  case newcolumntype_cmd:
    T_newcolumn_type();
    return;
  case epsfbox_cmd: 
    T_epsfbox();
    return;
  case put_cmd:
    T_put(c);
    return;
  case curves_cmd:
    T_curves(c);
    return;
  case dashline_cmd:
    T_dashline(c);
    return;
  case bezier_cmd:
    T_bezier(c);
    return;
  case verb_cmd:
    expand_verb0(c? verb_saved_char : 0);
    return;
  case gloss_cmd:
    T_gloss(c==0);
    return;
  case only_preamble_cmd:
    get_r_token(true);
    onlypreamble.push_back(hash_table.let_token);
    onlypreamble.push_back(cur_tok);
    onlypreamble.push_back(hash_table.notprerr_token);
    return;
  case toc_cmd: { // insert <tableofcontents/>
    name_positions np = np_toc;
    if(c==1) np = np_toc1;
    if(c==2) np = np_toc2;
    remove_initial_star();
    leave_h_mode ();
    the_stack.push1(np);
    if(c==0) {
      static bool inserted = false; 
      if (!inserted) the_stack.top_stack()->change_id(4);
      inserted = true;
      int k = eqtb_int_table[42+ count_reg_offset].get_val();
      Buffer& b = mac_buffer; b.reset() ; b << k;
      the_stack.add_att_to_cur(Istring("depth"), Istring(b));
    }
    the_stack.pop(np);
    return;
  }
  case begin_cmd:
  case end_cmd: 
    T_beginend(x);
    return;  
  case index_cmd:
    T_index(c);
    return;
  case document_cmd : 
    T_begindocument();
    return;
  case end_document_cmd:
    T_enddocument(c);
    return;
  case keywords_cmd:
    T_keywords();
    return;
  case end_keywords_cmd: 
    the_stack.pop(np_keywords);
    return;
  case center_cmd: 
    leave_h_mode(); // finish the possibly not-centered paragraph
    the_stack.add_nl(); // needed ?
    word_define(incentering_code,c,false);
    return;
  case end_center_cmd:
    leave_h_mode(); // finish centered paragraph
    return;
  case float_cmd:
    T_float(c);
    return;
  case figure_cmd:
  case table_cmd:
    T_figure_table(x,c);
    return;
  case end_figure_cmd: T_figure_table_end(true); return;
  case end_table_cmd: T_figure_table_end(false); return;
  case thebibliography_cmd:
    T_start_the_biblio();
    return;
  case end_thebibliography_cmd:
    T_end_the_biblio();
    return;
  case solvecite_cmd:
    solve_cite(false);
    return;
  case cite_one_cmd: T_cite_one();  return;
  case ipa_cmd: T_ipa(c);  return;
  case footcitepre_cmd:
    unprocessed_xml.push_back_unless_punct(' ');
    return;
  case ding_cmd:
    T_ding();
    return;
  case etex_cmd:  T_etex(c); return;
  case rule_cmd:
    scan_rule(c);
    return;
  case file_cmd:
    T_input(c);
    return;
  case unimp_font_cmd:
    T_unimplemented_font(c);
    return;
  case unimp_cmd:
    T_unimp(c);
    return;
  case specimp_cmd:
    T_specimp(c);
    return;
  case thickness_cmd:
    T_linethickness(c);
    return;
  case thm_aux_cmd:
    T_thm_aux(c);
    return;
  case start_thm_cmd:
    if(c==2) T_end_theorem();
    else T_start_theorem(c);
    return;
  case atdocument_cmd: 
    T_atdocument(c);
    return;
  case glossaire_cmd:
    T_glossaire();
    return;
  case end_glossaire_cmd:
    T_glossaire_end();
    return;
  case list_cmd: 
  case itemize_cmd: 
  case enumerate_cmd:
  case description_cmd: 
    T_listenv(x);
    return;
  case end_list_cmd:
  case end_itemize_cmd:
  case end_enumerate_cmd:
  case end_description_cmd:
    T_listenv_end(x);
    return;
  case xmlelement_env_cmd:  
    T_xmlenv(c);
    return;
  case end_xmlelement_env_cmd: 
    T_xmlenv_end(c);
    return;
  case filecontents_env_cmd:  
    T_filecontents(c);
    return;
  case addatt_cmd:
    T_xmladdatt(c);
    return;
  case ignore_env_cmd: 
    return;
  case ignore_content_cmd:
    ignore_env(false);
    return;
  case raw_env_cmd:
    the_stack.add_last(new Xml(Istring(ignore_env(true))));
    return;
  case math_env_cmd:
    pop_level(true,bt_env);
    T_math(c);
    return;
  case RAsection_env_cmd:  
    T_rasection();
    return;
  case end_RAsection_env_cmd:  
    T_rasection_end();
    return;
  case popmodule_cmd:
    the_stack.end_module();
    return;
  case pushmodule_cmd: push_module(); return;
  case tabular_env_cmd:
    T_start_tabular(c);
    return;
  case end_tabular_env_cmd:
    T_end_tabular(c);
    return;
  case Verbatim_env_cmd:
  case verbatim_env_cmd:
    special_verbatim();
    return;
  case picture_env_cmd:
    T_picture();
    return;
  case end_picture_env_cmd:
    the_stack.pop(np_picture);
    return;
  case end_ignore_env_cmd: 
    return;
  case minipage_cmd: 
    T_minipage();
    return;
  case end_minipage_cmd:
    flush_buffer();
    the_stack.pop_if_frame(the_names[cst_p]);
    the_stack.pop_if_frame(the_names[np_item]);
    the_stack.pop(np_minipage);
    return;
  case end_ignore_content_cmd:
  case end_raw_env_cmd:
  case end_math_env_cmd:
    parse_error("missing \\begin environment ",
		cur_tok.tok_to_str(), "missing begin");
    return;
  case eqno_cmd:

  case mathordb_cmd:
  case mathord_cmd:
  case mathbin_cmd: 
  case mathrel_cmd:
  case mathbetween_cmd:
  case mathopen_cmd:
  case mathclose_cmd:
  case mathop_cmd:
  case mathopn_cmd:
  case mathspace_cmd:
  case mathfont_cmd:
  case math_font_cmd:
  case math_list_cmd:
  case math_xml_cmd: 
  case left_cmd:
  case right_cmd:
    math_only();
    return;
  case mathinner_cmd:
    if(math_loc(c)==vdots_code) {
      back_input(hash_table.dollar_token);
      back_input(cur_tok);
      back_input(hash_table.dollar_token);
      return;
    }
    math_only();
    return;
  case endv_cmd:
    T_endv();
    return;
  case cr_cmd:
    T_cr();
    return;
  case cons_cmd: T_cons(); return;
  case self_insert_cmd:
    LC();
    unprocessed_xml.push_back(cur_tok);
    return;
  case check_date_cmd: date_commands(c); return;
  case typein_cmd: T_typein(); return;
  case testopt_cmd: T_testopt(); return;
  case selective_sanitize_cmd: selective_sanitize(); return;
  case latex_error_cmd: T_class_error(c); return;
  case change_element_name_cmd :  T_change_element_name(); return;
  case formatdate_cmd : formatdate(); return;
  case numberwithin_cmd : numberwithin(); return;
  case dblarg_cmd : dbl_arg(); return;
  case ifdefinable_cmd : ifdefinable(); return;
  case makelabel_cmd : makelabel(); return;
  case color_cmd: T_color(c); return;
  case kvo_family_cmd: kvo_family(c); return;
  default: 
    undefined_mac();
  }
}
