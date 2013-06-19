// Tralics, a LaTeX to XML translator.
// Copyright INRIA/apics/marelle (Jose' Grimm) 2002, 2004, 2007-2011

// This software is governed by the CeCILL license under French law and
// abiding by the rules of distribution of free software.  You can  use, 
// modify and/ or redistribute the software under the terms of the CeCILL
// license as circulated by CEA, CNRS and INRIA at the following URL
// "http://www.cecill.info". 
// (See the file COPYING in the main directory for details)


// This file contains a big part of the Tralics translator
#include "tralics.h"
const char* txtranslate_rcsid =
  "$Id: txtranslate.C,v 2.128 2011/06/28 08:15:25 grimm Exp $";


extern void readline_newprompt(string s);
extern void txsleep(int);

namespace {
  Buffer current_head;
  Buffer Tbuf;
  Buffer Abuf;
  Buffer tpa_buffer;
  Xmlp unfinished_par=0;

}

namespace xkv_ns {
  string to_string(TokenList& s,String msg);
}

namespace translate_ns {
  void seen_pack(string,int);
  void tex_message(String s);
  Istring find_color(const string& a, const string& b);
}
using namespace translate_ns;

namespace bib_ns {
  extern bool allow_break;
}

// This code translates everything, until end of file.
void Parser::translate_all()
{
  unprocessed_xml.reset();
  for(;;) {
    if(get_x_token()) break;
    if(tracing_commands()) translate02();
    translate01();
  }
  flush_buffer();
}

// Same, with a test that unprocessed_xml is empty
// In general, the loop in translate_all will stop because 
// restricted is true, and token list is exhausted
void Parser::translate0()
{
  if(!unprocessed_xml.empty()) missing_flush();
  translate_all();
}

// Main function: translates a token list.
// Result is added to the current element on the stack.
void Parser::T_translate(TokenList& X)
{
  if(X.empty()) return; 
  SaveState s;
  save_the_state(s);
  restricted = true;
  TL.swap(X);
  if(!unprocessed_xml.empty()) missing_flush();
  translate_all();
  restore_the_state(s);
}

// This prints the command to translate. The case of a space is special
// Letters are printed elsewhere 
void Parser::translate02()
{
  if(cur_cmd_chr.is_space()) { 
    if(!the_stack.in_v_mode())
      the_log.out_single_char(' '); 
    return;
  }
  if(cur_cmd_chr.is_letter_other())  return;
  the_log << lg_startbrace;
  print_cmd_chr(cur_cmd_chr);
  the_log << lg_endbrace;
}

// Flushes the buffer, creating a new xml element that will be added
// to the main tree.
void Parser::flush_buffer1()
{
  if(tracing_commands())
    the_log << lg_startbrace << "Text:" 
	    << unprocessed_xml.convert_to_log_encoding() << lg_endbrace;
  if(unprocessed_xml.empty()) cout << "EMPTY";
  the_stack.add_last_string(unprocessed_xml); 
  unprocessed_xml.reset();
}

// as above, does nothing if the buffer is empty,
void Parser::flush_buffer()
{
  if(!unprocessed_xml.empty()) 
    flush_buffer1();
}

// Same, but removes final newline. When TeX sees an empty line, it generates 
// 2 tokens: a space and \par. Here we remove the space.
void Parser::flush_buffer0()
{
  if(!unprocessed_xml.empty())
    flush_buffer1();
  the_stack.remove_last_space();
}

// Quits vertical mode. Said otherwise, opens a new paragraph.
void Parser::leave_v_mode()
{
  flush_buffer();
  if(the_stack.in_v_mode()) ileave_v_mode();
}

Xid Parser::ileave_v_mode()
{
  int k = cur_centering();
  Xid res = the_stack.push_par(k);
  if(unfinished_par) {
    res.add_attribute(unfinished_par->get_id().get_att());
    unfinished_par = 0;
  }
  return res;
}

// Quits horizontal mode. Said otherwise finishes a paragraph. May fail
// if the stack does not hold a <p>.
void Parser::leave_h_mode()
{
  if(the_stack.in_h_mode()) {
    unfinished_par = 0;
    flush_buffer0();
    the_stack.pop(cst_p);
    Xmlp p =the_stack.top_stack()->back();
    if(p) {
      if (p->is_empty_p()) { the_stack.top_stack()->pop_back(); p = 0; }
    }
    the_stack.add_nl();
  }
}


AttList& Parser::last_att_list()
{
  return the_stack.get_top_id().get_att();
}

inline string xml_to_string (Xmlp X)
{
  string s = X->convert_to_string();
  delete X;
  return s;
}

// translates {foo}
void Parser::T_next_arg()
{
  TokenList L = mac_arg();
  T_translate(L);
}

// translates {foo} in a group
void Parser::T_next_arg_local()
{
  push_level(bt_local);
  TokenList L = mac_arg();
  T_translate(L);
  pop_level(false,bt_local);
}

// translates [foo]
void Parser::T_next_optarg()
{
  TokenList L;
  next_optarg(L);
  T_translate(L);
}

// Translates a list of token in argument mode. Returns the value.
Xmlp Parser::translate_list(TokenList& L)
{
  Xmlp res = the_stack.temporary();
  T_translate(L);
  the_stack.pop(cst_argument);
  return res;
}

// Same, but reads the list of tokens.
Xmlp Parser::xT_next_arg()
{
  TokenList L = mac_arg_long();
  return translate_list(L);
}

// Same, but returns a string
string Parser::sT_next_arg()
{
  return xml_to_string(xT_next_arg());
}

// Return an Id location for the next argument
Istring Parser::nT_next_arg()
{
  string s = sT_next_arg();
  return Istring(s);
}

// Return 0 if the argument is empty or does not exist.
Xmlp Parser::xT_next_optarg()
{
  TokenList L;
  next_optarg(L);
  if(L.empty()) return 0;
  return translate_list(L);
}

// Hacked version of sT_next_arg.
string Parser::special_next_arg()
{
  InUrlHandler something;
  InLoadHandler something_else;
  SaveCatcode unused2('~',12);
  SaveCatcode unused3('#',13);
  return sT_next_arg();
}

// Returns next optional argument as a string
string Parser::sT_next_optarg()
{
  Xmlp res = xT_next_optarg();
  if(!res) return "";
  return xml_to_string(res);
}

// Returns next optional argument as an attribute value.
// return 0 if the argument is empty or does not exist.
Istring Parser::nT_next_optarg()
{
  TokenList L;
  next_optarg_long(L);
  if(L.empty()) return Istring();
  Xmlp x = translate_list(L);
  string y = xml_to_string(x);
  return Istring(y);
}

// Returns next arg as a string (not translated)
string Parser::rT_next_optarg()
{
  TokenList L;
  next_optarg_long(L);
  return exp_token_list_to_string(L);
}

string Parser::rT_next_arg()
{
  TokenList L = mac_arg_long();
  return exp_token_list_to_string(L);
}

// First argument of minipage should be [c] [t] or [b]
// Third arg allows [s]
// returns cst_invalid in case of failure, a position otherwise
// Fixme : should be outer, requires caller's name.
name_positions Parser::get_ctb_opt(bool all)
{
  TokenList L; 
  next_optarg_long(L);
  if(L.empty()) return cst_invalid;
  Token t = token_ns::get_unique(L);
  if(t.is_null()) return cst_invalid;
  if(t.cmd_val()!=letter_catcode) return cst_invalid;
  char c = t.val_as_letter();
  if(c=='c') return np_letter_c;
  if(c=='s') return np_letter_s;
  if(c=='t') return np_letter_t;
  if(c=='b') return np_letter_b;
  else return cst_invalid;
}

// Nodes: tblr, or 2 letters
name_positions Parser::get_trees_opt()
{
  TokenList L; 
  next_optarg_long(L);
  if(L.empty()) return cst_invalid;
  Token t1,t2;
  token_ns::get_unique(L,t1,t2);
  if(t1.is_null()) return cst_invalid;
  if(t1.cmd_val()!=letter_catcode) return cst_invalid;
  uint c = t1.val_as_letter();
  if(t2.is_null()) {
    if(c=='l') return np_letter_l;
    if(c=='r') return np_letter_r;
    if(c=='t') return np_letter_t;
    if(c=='b') return np_letter_b;
    else return cst_invalid;
  } 
  if(t2.cmd_val()!=letter_catcode) return cst_invalid;
  uint c2 = t2.val_as_letter();
  if(c!='t' && c != 'b') return cst_invalid;
  if(c2!='l' && c2 != 'r') return cst_invalid;
  if(c=='t') return c2=='l' ?  np_letters_tl : np_letters_tr; 
  else return c2=='l' ?  np_letters_bl : np_letters_br; 
}

// Second argument of \makebox \framebox should be [c] [l] or [r] or [s]
// returns cst_invalid in case of failure, a position otherwise
name_positions Parser::get_lrcs_opt(bool all)
{
  TokenList L; 
  next_optarg_long(L);
  if(L.empty()) return cst_invalid;
  Token t = token_ns::get_unique(L);
  if(t.is_null()) return cst_invalid;
  if(t.cmd_val()!=letter_catcode) return cst_invalid;
  uint c = t.val_as_letter();
  if(c=='c') return np_letter_c;
  if(c=='l') return np_letter_l;
  if(c=='r') return np_letter_r;
  if(c=='s') return np_letter_s;
  else return cst_invalid;
}

// translates a token list, result is a string
string Parser::sT_translate(TokenList& L)
{
  return xml_to_string(translate_list(L));
}

// In the case where the font has changed and we are in text mode, we call 
// check_font.
void Parser::font_has_changed1()
{
  if(!the_stack.in_v_mode() && !cur_font.is_ok ()) {
    flush_buffer();
    the_stack.check_font();
  }
}

// This is done whenever we see an explicit font change.
void Parser::font_has_changed()
{
  cur_font.pack();
  save_font();
  font_has_changed1();
  cur_font.update_old();
}

// Case of a command like \rmfamily.
void Parser::see_font_change(subtypes c)
{
  cur_font.see_font_change(c);
  font_has_changed();
}

// Case of a command like \small.
void Parser::translate_font_size ()
{
  cur_font.change_size(cur_cmd_chr.get_chr());
  font_has_changed();
}

// \rm is : \normalfont\rmfamily (in latex, it changes also math fonts)
void Parser::old_font()
{
  cur_font.kill();
  see_font_change(cur_cmd_chr.get_chr());
}

// \textit{x} is coded as {\itshape x}
void Parser::arg_font(subtypes c)
{
  flush_buffer();
  before_mac_arg(name_for_error);
  push_level(bt_brace);
  see_font_change(c);
}

// \textsuperscript, \textsubscript, etc.
void Parser::T_fonts(name_positions x)
{
  leave_v_mode();
  Xmlp res = the_stack.fonts1(x);
  the_stack.push(the_names[cst_fonts],res);
  T_next_arg();
  the_stack.pop(cst_fonts);
}


// \indent or \noindent. If we are in a <p>,  and the <p> is empty, then we 
// add noindent=true or noindent=false, unless there is already a noindent 
// attribute. If we are in a <p>, cur par is not empty, we first close the <p>
// if we are not in <p>, we open a new paragraph.
void Parser::implicit_par(subtypes c)
{
  bool noindent = c==zero_code;
  flush_buffer();
  if(the_stack.in_h_mode()&& cur_centering()!=1) {
    Xmlp cp = the_stack.get_cur_par();
    if(!cp) {
      parse_error("Invalid \\par command");
      return;
    }
    if(cp->par_is_empty()) {
      cp->get_id().add_attribute(np_noindent,the_main->SH.st_bool(noindent),false);
      return;
    }
  }
  finish_par_cmd(noindent,Istring());
}

void Parser::finish_par_cmd(bool noindent, Istring xs)
{
  leave_h_mode();
  int k = cur_centering();
  Xid id = ileave_v_mode();
  if(!xs.null()) id.add_attribute(the_names[np_spacebefore], xs);
  if(k!=1) id.add_attribute(np_noindent,the_main->SH.st_bool(noindent));
}

// Translation of \par
void Parser::T_par1()
{
  if(the_stack.in_v_mode()) return;
  if(the_stack.in_no_mode()) return;
  Istring frame = the_stack.first_frame();
  if(frame == the_names[cst_hbox]) return;
  flush_buffer();
  if(the_stack.in_h_mode()) {
    Xmlp cp = the_stack.get_cur_par();
    if(!cp) {
      parse_error("Invalid \\par command: paragraph not started");
    } else if(cp->par_is_empty()) {
      unfinished_par = cp;
      cp->change_name(Istring());
      the_stack.pop(cst_p);
      return;
    }
  }
  leave_h_mode();
}

// Translation of \\[dim], u is the att_val of the dimension
void Parser::T_par1(Istring u)
{
  flush_buffer();
  finish_par_cmd(true,u);
}

// User function that adds some element. Is leave_v_mode necessary ?
// if w generates <foo/> otherwise <foo></foo>
void Parser::T_xmlelt(subtypes w)
{
  flush_buffer();
  string s = sT_next_arg();
  if(w) { unprocessed_xml << "<" <<  s << "/>" ; flush_buffer();return ; }
  leave_v_mode();
  Istring name  = Istring("xmlelt");
  the_stack.push(name, new Xml(Istring(s),0));
  T_next_arg();
  the_stack.pop(name);
}

// Special case where the XML name and the frame name are the same
// Note the additional braces
void Parser::T_arg1(name_positions y)
{
  the_stack.push1(y);
  TokenList L = mac_arg();
  brace_me(L);
  T_translate(L);
  the_stack.pop(y);
}

// Case of an item in a list... 
void Parser::T_item (int c)
{
  leave_h_mode();
  unfinished_par = 0;
  the_stack.pop_if_frame(the_names[np_item]);
  skip_initial_space_and_back_input();
  Istring att = T_item_label(c);
  the_stack.add_nl();
  the_stack.push1(np_item);
  if(!att.null())
    the_stack.add_att_to_last(np_label_item,att);
  the_stack.add_new_anchor();
  the_stack.set_v_mode();
  skip_initial_space_and_back_input();
  finish_par_cmd(true,Istring());
}

// c=1 in the case of \@item (new scheme)
Istring Parser::T_item_label(int c)
{
  TokenList L;
  bool opt = cur_tok.is_open_bracket();
  if(opt) next_optarg_long(L); 
  string list_ctr = the_parser.eqtb_string_table[1].get_val();
  if(!list_ctr.empty())
    refstepcounter(list_ctr,false);
  if(!opt) {
    Token t = hash_table.itemlabel_token;
    token_from_list(t);
    if(!cur_cmd_chr.is_relax()) 
      L.push_back(t);
    else return Istring(0);
  }
  brace_me(L); // \item[\bf x] puts only x in \bf
  the_stack.push1(np_label_item);
  the_stack.set_arg_mode();
  T_translate(L);
  the_stack.pop(np_label_item);
  if(!(c || get_cur_env_name()=="enumerate")) return Istring(0);
  Xmlp res = the_stack.remove_last();
  res->change_name(Istring(1));
  string w = xml_to_string(res);
  return Istring(w);
}


// Case of \glo{item}{some text} inside an glossaire env.
void Parser::T_glo ()
{
  leave_h_mode(); // mode should be no_mode now.
  mode w = the_stack.get_mode();
  the_stack.set_arg_mode();
  T_arg1(np_label_glo);
  the_stack.push1(np_item);
  the_stack.set_v_mode();
  T_next_arg();
  leave_h_mode();
  the_stack.pop(np_item);
  the_stack.set_mode(w);
}

// Case of \chapter, \section, etc. Here Y is the title, and y the index
// (it is 7 for old module, 8 for new module).
// Since there are no more old modules  7 is replaced by 8
// We have opened the current XML element, 
// we have to allow for a label and read the title.
void Parser::start_paras(int y, string Y,bool star)
{
  if(!star) {
    if(y==0)
      the_stack.add_new_anchor_spec();
    else
      the_stack.add_new_anchor();
  }
  bool module =y==7 ||  y == 8;
  Xmlp opt = 0;
  if(module)
    add_id("mod:" +Y);
  else
    opt = xT_next_optarg();
  the_stack.set_arg_mode();
  the_stack.push1(np_head);
  Xmlp title = the_stack.top_stack();
  TokenList L = mac_arg();
  if(module) check_module_title(L);
  brace_me(L);
  T_translate(L);
  current_head.reset();
  title->put_in_buffer(current_head);
  the_stack.pop(np_head);
  if(opt) the_stack.add_last(new Xml(np_alt_section,opt));
  the_stack.add_nl();
  the_stack.set_v_mode();
  static int first_print_level = 10;
  if(module) first_print_level =  0;
  String YY = current_head.convert_to_log_encoding();
  if(y<=first_print_level) {
    first_print_level = y;
    cout << "Translating section command " << Y << ": " << YY << ".\n";
  }
  the_log << lg_start << "Translating " << Y << ": " << YY << ".\n";
}


// An error is signaled if the title of the module is empty
void Parser::check_module_title(TokenList&L)
{
  static int ctr =0;
  ++ctr;
  token_ns::remove_initial_spaces(L);
  if(ctr==1) return; // ok for the first module
  if(!L.empty()) return;
  signal_error("Empty module name replaced!");
  L = token_ns::string_to_list(ctr==2 ? "Overall Objectives" :
			       "Introduction",false);
}

// Translates \frontmatter, \mainmatter, \backmatter
void Parser::T_matter(subtypes c)
{
  leave_h_mode();
  the_stack.end_module();
  the_stack.add_nl();
  name_positions t = np_mainmatter;
  if(c==frontmatter_code) t =np_frontmatter;
  else if(c==backmatter_code) t =np_backmatter;
  the_stack.push1(the_names[np_module],t);
  if(c==frontmatter_code || c== backmatter_code) 
    chapter_has_star = true;
  else
    chapter_has_star = false;
}

// Translates \section, \chapter, etc.
void Parser::T_paras(subtypes x)
{
  int y = x-sectionning_offset;
  if(x==toplevel_sec_code) {
    y = 0;
    TokenList L = mac_arg();
    if(!L.empty()) {
      token_from_list(L.front());
      sectionning_offset = section_code;
      if (cur_cmd_chr.get_cmd()==section_cmd)
	sectionning_offset = cur_cmd_chr.get_chr();
      if(sectionning_offset == chapter_code) 
	Xid(1).add_attribute(np_chapters,np_true);
      if(sectionning_offset == part_code) 
	Xid(1).add_attribute(np_part,np_true);
    }
    return;
  }
  if(x==endsec_code) {
    y = 0;
    TokenList L = mac_arg();
    if(!L.empty()) {
      token_from_list(L.front());
      if (cur_cmd_chr.get_cmd()==section_cmd)
	y = cur_cmd_chr.get_chr() - sectionning_offset;
    }
  }
  if(y<0) y = 0;
  if(y>6) y = 6;
  Istring Y = the_names[cst_div0+y];
  leave_h_mode();
  the_stack.para_aux(y); // this pops the stack...
  the_stack.add_nl();
  if(x==endsec_code) return;
  the_stack.push1(Y,name_positions(np_div0+y));
  bool star = remove_initial_star();
  if(chapter_has_star && x==chapter_code) star = true;
  if(star)
    last_att_list().push_back(np_rend,np_nonumber);
  else {
    if(x==part_code) refstepcounter("part",false);
    else if(x==chapter_code) refstepcounter("chapter",false);
    else if(x==section_code) refstepcounter("section",false);
    else if(x==subsection_code) refstepcounter("subsection",false);
    else if(x==subsubsection_code) refstepcounter("subsubsection",false);
    else if(x==paragraph_code) refstepcounter("paragraph",false);
    else if(x==subparagraph_code) refstepcounter("subparagraph",false);
    else star = true;
  }
  start_paras(y,Y.c_str(),star);
}

// Translates \label or \ref. This is mode independent (ok ?)
void Parser::T_label()
{
  string a = special_next_arg();
  add_id(a);
}

void Parser::T_ref(bool is_ref)
{
  string a = special_next_arg();
  the_stack.add_newid0(np_ref);
  Xid X = the_stack.get_xid();
  X.add_ref(a);
  if(!is_ref) X.add_attribute(np_rend,np_page);
}

// Commands for the float package
void Parser::T_float(subtypes c)
{
  switch(c) {
  case 0: //@float
  case 1:// @dblfloat
    {
      Buffer& B=Tbuf;
      string sarg = sT_next_arg();
      Istring arg = Istring(sarg);
      Istring opt = nT_next_optarg();
      if(opt.null()) {
	B <<bf_reset << "fps@" << sarg;
	expand_no_arg(B.to_string());
	opt = nT_next_arg();
      }
      word_define(incentering_code,1,false);
      leave_h_mode();
      the_stack.push1(np_float);
      the_stack.add_att_to_last(np_place,opt);
      if(c==1) the_stack.add_att_to_last(np_starred,np_true);
      the_stack.add_att_to_last(np_type,arg);
      B <<bf_reset << "fname@" << sarg;
      expand_no_arg(B.to_string());
      opt = nT_next_arg();
      the_stack.add_att_to_last(np_name,opt);
      refstepcounter(sarg,true);
      B <<bf_reset << "@float@every@" << sarg;
      back_input(hash_table.locate(B));
      back_input(hash_table.locate("the"));
      B <<bf_reset << "fst@" << sarg;
      back_input(hash_table.locate(B));
      the_stack.set_v_mode(); 
      return;
    }
  case 2://float@end
  case 3://float@dblend
    leave_h_mode();
     if(!the_stack.top_stack()->has_name(np_float)) 
      parse_error("no float on stack");
    the_stack.pop(np_float);
    the_stack.add_nl();
    return;
  default:
    return;
  }
}


// Subfigure. Should appear only in a figure env.
void Parser::T_subfigure ()
{
  leave_v_mode();
  the_stack.push1(np_subfigure);
  refstepcounter("subfigure",true);
  the_stack.set_arg_mode();
  the_stack.push1(np_leg);
  T_next_optarg ();
  the_stack.pop(np_leg);
  {
    SaveCatcode tmp('_',13); // allow underscore in the file name (needed ?)
    T_arg1(np_texte);
  }
  the_stack.pop(np_subfigure);
}

// Case of &. Works only inside a table (math code is elsewhere).
void Parser::T_ampersand()
{
  if(the_stack.is_frame(np_cell))
    finish_a_cell(hash_table.endv_token,Istring());
  else if(the_stack.is_frame2(cst_hanl)) {
    LC(); 
    unprocessed_xml << "&amp;";  // hack...
    return;
  } else
    parse_error("Unexpected &","Unexpected ampersand");
}

// This interprets \newline.
void Parser::T_newline() 
{
  if(the_stack.is_frame(cst_p))
    T_par1(Istring());
  else if(the_stack.in_v_mode()) return;
  else
    parse_error("Bad use of \\newline");
}

// This interprets \\.
void Parser::T_backslash() 
{
  Token T = cur_tok;
  flush_buffer();
  remove_initial_star();
  Istring a = get_opt_dim(T);
  if(the_stack.is_frame(np_head))
    back_input(hash_table.headercr_token);
  else if(the_stack.is_frame(cst_p)) { 
    T_par1(a); 
    remove_initial_space_and_back_input(); // Needed because we are in hmode
  } else if(the_stack.is_frame(np_cell))
    finish_a_cell(hash_table.cr_token,a);
  else if(the_stack.in_v_mode()) return;
  else if(the_stack.is_omit_cell())
    back_input(hash_table.space_token);
  else
    parse_error("Bad use of \\\\");
}

// This is the code of \begin{motscle}
void Parser::T_keywords ()
{
  leave_h_mode();
  the_stack.push1(np_keywords);
  the_stack.add_nl();
  the_stack.set_no_mode();
  bool seen_end = false;
  for(;;) {
    TokenList v;
    grab_env(v,true,seen_end);
    token_ns::remove_first_last_space(v);
    if(!v.empty() && v.back().is_dot())
      v.pop_back();
    the_stack.push1(np_term);
    the_stack.set_arg_mode();
    T_translate(v);
    the_stack.pop(np_term);
    the_stack.add_nl();
    if(seen_end) break;
  }
}

 

// Handle the case of an argument of \includegraphics
// Extension is in Abuf, if needed
void Parser::no_extension (AttList& AL,const string& s)
{
  int k =-1;
  bool ok = true;
  Tbuf.reset();
  Tbuf.push_back(s);
  int i = 0;
  int ii=0;
  for(;;) {
    if(Tbuf[i]=='.'  && Tbuf[i+1]=='.' && Tbuf[i+2] =='/') { i+=3; continue; }
    if(Tbuf[i]=='.'  && Tbuf[i+1] =='/') {
      if(ii==0) ii=2; // compatibility
      i+=2; continue;
    }
    break;
  }
  if(Tbuf[i]=='.') ok = false;
  for(;;i++) {
    if(Tbuf[i]==0) { if(k==i-1) ok = false; break; }
    if(Tbuf[i]=='.') { 
      if(k==-1) k = i; 
      else { ok = false; break; }
    }
  }
  if(!ok) {
    TokenList L = Tbuf.str_toks11(false);
    brace_me(L);
    back_input(L);
    back_input(hash_table.locate("@filedoterr"));
  }
  if(ok && k>0) {
    AL.push_back(np_fileextension, Istring(Tbuf.c_str(k+1)));
    Tbuf.kill_at(k);
  }
  enter_file_in_table(Tbuf.to_string(ii),ok);
  AL.push_back(np_file,Istring(Tbuf));
}

void Parser::default_bp(Buffer& B,Token T,TokenList&val)
{
  val.push_back(Token(letter_t_offset,'b'));
  val.push_back(Token(letter_t_offset,'p'));
  dimen_from_list0(T,val);
  B.push_back(cur_val.get_dim_val(),glue_spec_pt);
}

// chr =0 for \includegraphics, 1 for \psfig
void Parser::includegraphics(subtypes C) 
{
  bool ic = C==0;
  String bkey= "Bad key in argument of of includegraphics";
  String bval= "Bad value in argument of includegraphics";
  Token T = cur_tok;
  remove_initial_star();
  Token comma = hash_table.comma_token;
  Token equals = Token(other_t_offset,'=');
  TokenList W,val,key;
  string file_name;
  {
    InLoadHandler something;
    if(ic) {
      next_optarg(W);
      flush_buffer();
      file_name = sT_next_arg(); 
    } else W = mac_arg();
  }
  expand_no_arg("Gin@keys");
  { TokenList K = mac_arg(); W.splice(W.begin(),K); }
  leave_v_mode();
  AttList& AL = the_stack.add_newid0(np_figure);
  if(ic) no_extension(AL,file_name);
  Buffer&B = tpa_buffer;
  while(!W.empty()) {
    token_ns::split_at(comma,W,val);
    token_ns::remove_initial_spaces(val);
    if(val.empty()) continue;
    token_ns::split_at(equals,val,key);
    token_ns::remove_first_last_space(key);
    if(key.empty()) continue;
    token_ns::remove_first_last_space(val);
    string skey = xkv_ns::to_string(key,bkey);
    B.reset();
    if(!ic && (skey == "file" || skey == "figure")) { 
      string sval = xkv_ns::to_string(val,bval);
      no_extension(AL,sval); 
      ic = true;
    } else if(skey == "keepaspectratio" || skey=="clip" || skey=="draft" || skey =="hiresbb") {
      name_positions V = np_true;
      string sval = xkv_ns::to_string(val,bval);
      if(sval=="false") V = np_false;
      if(skey == "clip") AL.push_back(np_clip, V,true);
      else AL.push_back(Istring(skey), V,true);
    } else if(skey == "type" || skey=="ext" ||skey=="read" ||skey=="command"
	      ||skey=="origin" || skey=="scale" || skey=="angle") {
      string sval = xkv_ns::to_string(val,bval);
      if(skey=="angle" && sval=="0") continue;
      Istring p = skey=="scale" ? the_names[np_scale] :
	skey=="angle" ? the_names[np_angle] : Istring(skey);
      AL.push_back(p, Istring(sval),true);
    } else if(skey == "width" || skey=="height" ||skey=="totalheight") {
      name_positions N = skey=="height" ? np_height : skey=="width" ?
	np_width : np_totalwidth;
      ScaledInt s = dimen_from_list(T,val);
      B.push_back(s,glue_spec_pt);
      AL.push_back(the_names[N], Istring(B),true);
    } else if(skey=="natwidth" || skey=="natheight" || skey=="bbllx" || 
	      skey=="bblly" || skey=="bburx" || skey=="bbury") {
      default_bp(B,T,val);
      AL.push_back(Istring(skey), Istring(B),true);	    
    } else if(skey=="bb" || skey=="viewport" || skey=="trim") {
      TokenList aux;
      Token SP = Token(space_token_val);
      for(int i = 0;i<4;i++) {
	token_ns::remove_initial_spaces(val);
	val.push_back(SP);
	token_ns::split_at(SP,val,aux);
	default_bp(B,T,aux);
	if(i<3) B.push_back(' ');
      }
      AL.push_back(Istring(skey), Istring(B),true);	    
    } else invalid_key(skey,val);
  }
  AL.push_back(np_rend,np_inline);
}

void Parser::T_epsfbox()
{
  int xdim_pos = dimen_reg_offset+11;
  int ydim_pos = dimen_reg_offset+12;
  ScaledInt xdim = eqtb_dim_table[xdim_pos].get_val();
  ScaledInt ydim = eqtb_dim_table[ydim_pos].get_val();
  flush_buffer();
  string y;
  {
    InLoadHandler something;
    y = sT_next_arg();
  }
  AttList& res = the_stack.add_newid0(np_figure);
  no_extension(res,y);
  res.push_back(np_rend,np_inline);
  if(!xdim.null()) 
    res.push_back(np_width,the_main->SH.find_scaled(xdim));
  if(!ydim.null()) 
    res.push_back(np_height,the_main->SH.find_scaled(ydim));
  dim_define(xdim_pos,ScaledInt(0),false); // reset to 0
  dim_define(ydim_pos,ScaledInt(0),false);
}

// \hspace or \vspace. Expands to \hskip argument\relax.
void Parser::T_hspace(subtypes c)
{
  Token t = cur_tok;
  remove_initial_star();
  scan_glue(it_glue,t,false);
  append_glue(ScaledInt(cur_val.get_glue_width()),c==1);
}

// Code of \vspace, or \vskip, after we have fetched the dimension.
void Parser::append_glue(ScaledInt dimen, bool vert)
{
  if(!vert) {
    int dim = dimen.get_value();
    leave_v_mode();
    int k = ((dim>>16)+2)/4;
    while(k>0) {
      k--;
      process_char(Utf8Char(0xA0));
    }
    return;
  }
  // Now we have a \vskip.
  if(the_stack.in_h_mode()) {
    flush_buffer();
    Xmlp cp = the_stack.get_cur_par();
    if(!cp) {
      parse_error("Expected a p element on the stack"); //  this is bad.
      return;
    }
    if(cp->par_is_empty()) {
      add_vspace(dimen,cp->get_id());
      return;
    }
  }
  leave_h_mode(); // start a new par if needed.
  if(!the_stack.in_v_mode()) return;
  Xid cp = ileave_v_mode();
  add_vspace(dimen,cp);
}

class ColSpec {
  string name;
  string model;
  string value;
  Istring id;
  Xmlp xval;
  bool used;
public:
  ColSpec(string a, string b,string c);
  bool compare(string a, string b) { return model==a && value==b; } 
  Istring get_id() { used = true; return id; }
  bool is_used() const { return used; }
  Xmlp get_val() const { return xval; }
};
vector<ColSpec*>all_colors;


ColSpec::ColSpec(string a, string b,string c) :
  name(a), model(b), value(c), used(false)
{
  xval = new Xml(np_color,0);
  if(!name.empty())
    xval->get_id().add_attribute(Istring("name"), Istring(name));
  xval->get_id().add_attribute(Istring("model"), Istring(model));
  xval->get_id().add_attribute(Istring("value"), Istring(value));
  Buffer&B =tpa_buffer;
  B.reset();
  static int n = 0;
  ++n;
  B << "colid" << n;
  id = Istring(B);
  xval->get_id().add_attribute(np_id, id);
}

void Parser::finish_color()
{
  int n = all_colors.size();
  int k = 0;
  for (int i=0;i<n;i++) if(all_colors[i]->is_used()) k++;
  if(k==0) return;
  Xmlp res = new Xml(Istring("colorpool"),0);
  for (int i=0;i<n;i++)
    if(all_colors[i]->is_used()) {
      res->push_back(all_colors[i]->get_val());
      res->add_nl();
    }
  the_stack.document_element()->replace_first(res);
}

// Find a color in the stack, returns the id;
// May add a new item to the stack
Istring translate_ns::find_color(const string& model, const string& value)
{
  int n= all_colors.size();
  for(int i=0;i<n;i++)
    if(all_colors[i]->compare(model,value)) return all_colors[i]->get_id();
  all_colors.push_back(new ColSpec("",model,value));
  return all_colors[n]->get_id();
}

// case of \color{red} or \color[rgb]{1,0,0}
// Marks the color as used
Istring Parser::scan_color(const string& opt,const string& name)
{
  if(opt.empty() || opt=="named") { // case \color{red} or \color[named]{Red}
    Buffer&B = tpa_buffer;
    B<< bf_reset <<  "\\color@" << name;
    token_from_list(hash_table.locate(B));
    if(cur_cmd_chr.get_cmd()==color_cmd) {
      int n = all_colors.size();
      int k = cur_cmd_chr.get_chr()-10;
      if(k>=0 && k<n) 
	return all_colors[k]->get_id();
    } 
    parse_error("Undefined color ", name, "undefined color");
    return Istring();
  } else return find_color(opt,name);
}


// All commands that deal with colors
void Parser::T_color(subtypes c)
{
  Buffer&B =tpa_buffer;
  flush_buffer();
  if(c==normalcolor_code) {
    cur_font.set_color(Istring(0));
    font_has_changed();
    return;
  }
  if(c==color_code) {
    string opt = sT_next_optarg();
    string name = sT_next_arg();
    Istring C = scan_color(opt,name);
    cur_font.set_color(C);
    font_has_changed();
    return;
  }
  if(c==pagecolor_code) {
    string opt = sT_next_optarg();
    string name = sT_next_arg();
    Istring C = scan_color(opt,name);
    AttList& res = the_stack.add_newid0(np_pagecolor);
    res.push_back(np_color,C);
  }
  if(c==colorbox_code) {
    string opt = sT_next_optarg();
    string name = sT_next_arg();
    Istring C = scan_color(opt,name);
    Xmlp mbox = internal_makebox();
    mbox->get_id().add_attribute(np_color,C);
    return;
  }
  if(c==fcolorbox_code) {
    string opt = sT_next_optarg();
    string name1 = sT_next_arg();
    string name2 = sT_next_arg();
    Istring C1 = scan_color(opt,name1);
    Istring C2 = scan_color(opt,name2);
    Xmlp mbox = internal_makebox();
    mbox->get_id().add_attribute(np_color,C1);
    mbox->get_id().add_attribute(np_color2,C2);
    return;
  }
  if(c==definecolor_code) { 
    string name = sT_next_arg();
    string model = sT_next_arg();
    string value = sT_next_arg();
    B<< bf_reset <<  "\\color@" << name;
    cur_tok = hash_table.locate(B);
    if(!hash_table.eqtb[cur_tok.eqtb_loc()].is_undefined()) 
      main_ns::log_and_tty << "Redefining color " << name << "\n";
    if(model=="named") {
      Buffer&B = tpa_buffer;
      B<< bf_reset <<  "\\color@" << value;
      Token T = hash_table.locate(B);
      eval_let(cur_tok,T,true);
      return;
    }
    int n = all_colors.size();
    all_colors.push_back(new ColSpec(name,model,value));
    CmdChr v(color_cmd,subtypes(n+10));
    eq_define(cur_tok.eqtb_loc(),v,true);
    return;
  }
  int n = all_colors.size();
  int k = c-10;
  if(k>=0 && k<n) {
    Istring C = all_colors[k]->get_id();
    cur_font.set_color(C);
    font_has_changed();
  }
}

Xmlp Parser::internal_makebox()
{
  leave_v_mode();
  the_stack.push1(np_mbox);
  Xmlp mbox = the_stack.top_stack();
  TokenList d=mac_arg();
  brace_me(d);
  T_translate(d);
  the_stack.pop(np_mbox);
  return mbox;
}

// Add the given dimension as spacebefore value to the paragraph x.
void Parser::add_vspace(ScaledInt dimen, Xid x)
{
  AttList&L = x.get_att();
  int K = L.has_value(the_names[np_spacebefore]);
  if(K>=0) {
    Istring k = L.get_val(K);
    scan_glue(it_glue,the_parser.hash_table.relax_token,k.c_str());
    dimen += ScaledInt(cur_val.get_glue_width());
  }
  Istring k = the_main->SH.find_scaled(dimen);
  x.add_attribute(the_names[np_spacebefore],k,true);
}

// Translates \makebox and \mbox
void Parser::T_mbox(subtypes c)
{
  Token T= cur_tok;
  Istring ipos;
  Istring iwidth;
  if(c==makebox_code) {
    flush_buffer();
    skip_initial_space_and_back_input();
    if(cur_tok.is_open_paren()) {
      T_makebox(false,T);
      return;
    }
    iwidth = get_opt_dim(T);
    ipos = the_names[get_lrcs_opt(true)];
  }
  Xmlp mbox = internal_makebox();
  if(!ipos.null() || !iwidth.null()) {
    mbox->get_id().add_attribute(np_box_pos,ipos);
    mbox->get_id().add_attribute(np_box_width,iwidth);
    return;
  }

  Xmlp u = mbox->single_non_empty();
  if(u && u->has_name(the_names[np_figure])) mbox->kill_name();
  if(mbox->only_text()) mbox->kill_name();
  if(mbox->only_hi()) mbox->kill_name();
}



// This translates \caption or \footnote
void Parser::T_cap_or_note(bool cap)
{
  leave_v_mode();
  name_positions name = cap ? np_caption : np_footnote;
  push_level(bt_local);
  word_define(incentering_code,0,false);
  the_stack.push1(name);
  Xmlp opt=0, note = 0;
  if(cap) {  // case of Caption
    opt = xT_next_optarg();
    note = the_stack.top_stack();
    the_stack.set_v_mode();
    T_next_arg();
    leave_h_mode();
  } else { // case of footnote, locally redefines fonts
    ignore_next_optarg(); // is this OK ?
    the_stack.add_att_to_last(the_names[np_place],the_names[np_foot]);
    my_stats.one_more_footnote();
    refstepcounter("footnote",true);
    note = the_stack.top_stack();
    FontInfo sv= cur_font;
    cur_font.kill(); cur_font.set_color(Istring());
    cur_font.change_size(6);
    font_has_changed();
    the_stack.set_v_mode();
    T_next_arg();
    leave_h_mode();
    cur_font = sv; 
    font_has_changed();
  }
  the_stack.pop(name);
  if(opt) the_stack.add_last(new Xml(np_alt_caption,opt));
  pop_level(false,bt_local);
  if(the_main->get_footnote_hack())
    note->remove_par_bal_if_ok(); 
}

void Parser::T_makebox(bool framed, Token C)
{
  Istring A,B;
  T_twodims(A,B,C);
  string oarg = sT_next_optarg();
  leave_v_mode();
  the_stack.push1(np_box);
  AttList& cur = last_att_list();
  if(framed)
    cur.push_back(np_framed,np_true);
  if(!oarg.empty())
    cur.push_back(np_box_pos,Istring(oarg));
  cur.push_back(np_height,B);
  cur.push_back(np_width,A);
  T_next_arg_local();
  the_stack.pop(np_box);
}

// Implements \sbox and \savebox
void Parser::T_save_box(bool simple)
{
  Token T = cur_tok;
  TokenList L = mac_arg();
  back_input(hash_table.equals_token);
  back_input(L);
  int i = scan_eight_bit_int();
  scan_optional_equals();
  skip_initial_space_and_back_input();
  //  leave_v_mode();
  flush_buffer();
  if(!simple && cur_tok.is_open_paren()) {
    T_makebox(false,T);
  } else {
    Istring ipos;
    Istring iwidth;
    if(!simple) {
      iwidth = get_opt_dim(T);
      ipos = the_names[get_lrcs_opt(true)];
    }
    the_stack.push1(np_mbox);
    the_stack.set_arg_mode();
    Xmlp mbox = the_stack.top_stack();
    TokenList d=mac_arg();
    brace_me(d);
    T_translate(d);
    the_stack.pop(np_mbox);
    mbox->get_id().add_attribute(np_box_pos,ipos);
    mbox->get_id().add_attribute(np_box_width,iwidth);
  }
  box_end(the_stack.remove_last(),i);
}

void Parser::T_picture()
{
  flush_buffer();
  the_stack.push1(np_picture);

  AttList& cur = last_att_list();
  the_stack.set_arg_mode();
  Istring A,B;
  Token C = cur_tok;
  T_twodims(A,B,C); 
  cur.push_back(np_height,B);
  cur.push_back(np_width,A);
  skip_initial_space_and_back_input();
  if(cur_tok.is_open_paren()) {
    T_twodims(A,B,C); 
    cur.push_back(np_ypos,B);
    cur.push_back(np_xpos,A);
  }
}

void Parser::T_fbox_dash_box()
{
  Token T = cur_tok;
  flush_buffer();
  TokenList a = mac_arg();
  Istring A = token_list_to_att(a,T,false);
  Istring B,C;
  T_twodims(B,C,T);
  string oarg = sT_next_optarg();
  the_stack.push1(np_dashbox);
  Xid cur_id = the_stack.get_top_id();
  if(!oarg.empty())
    cur_id.add_attribute(np_box_pos,Istring(oarg));
  cur_id.add_attribute(np_height,C);
  cur_id.add_attribute(np_width,B);
  cur_id.add_attribute(np_dashdim,A);
  T_next_arg_local();
  the_stack.pop(np_dashbox);
}

void Parser::T_fbox_rotate_box ()
{
  Token T = cur_tok;
  flush_buffer();
  Istring val = nT_next_arg(); 
  leave_v_mode();
  the_stack.push1(np_rotatebox);
  the_stack.get_top_id().add_attribute(np_r_angle,val);
  T_next_arg_local();
  the_stack.pop(np_rotatebox);
}

// \fbox{\includegraphics{...}} is special.
// This implements \fbox, \framebox and \scalebox
void Parser::T_fbox (subtypes cc)
{
  Token T = cur_tok;
  flush_buffer();
  if(cc==raisebox_code) { T_raisebox(); return; }
  Istring iscale;
  Istring ipos;
  Istring iwidth;

  if(cc==framebox_code) { // case of \framebox
    skip_initial_space_and_back_input();
    if(cur_tok.is_open_paren()) {
      T_makebox(true,T);
      return;
    }
    iwidth = get_opt_dim(T);
    ipos = Istring(get_lrcs_opt(true));
  }
  if(cc==scalebox_code) {
    flush_buffer();
    iscale =  nT_next_arg();
    iwidth =  nT_next_optarg();
  }
  leave_v_mode();
  the_stack.push1(cc==scalebox_code? np_sbox:np_fbox);
  Xmlp cur = the_stack.top_stack(); // will contain the argument.
  T_next_arg_local();
  the_stack.pop(cc==scalebox_code? np_sbox:np_fbox);
  Xmlp aux = cur->single_non_empty();
  AttList& AL = cur->get_id().get_att();
  if(cc==scalebox_code) {
    if(aux && aux->has_name(the_names[np_figure])) {
      aux->get_id().add_attribute(the_names[np_scale],iscale,true);
      aux->get_id().add_attribute(Istring("vscale"),iwidth);
      cur->kill_name();
    } else {
      AL.push_back(np_s_scale,iscale);
      AL.push_back(Istring("vscale"),iwidth);
    } 
    return;
  }
  if(aux && aux->has_name(the_names[np_figure])) {
    aux->get_id().add_attribute(np_framed,np_true);
    cur->kill_name();
  } else {
    AL.push_back(np_b_rend,np_boxed);
    AL.push_back(np_box_pos,ipos);
    AL.push_back(np_box_width,iwidth);
  }
}

// Returns <xref url='v'>val</xref>
void Parser::new_xref(Xmlp val, string v, bool err)
{
  my_stats.one_more_href();
  the_stack.add_last(new Xml(the_names[np_xref],val));
  the_stack.add_att_to_last(the_names[np_url],Istring(v));
  if(err&&(v.empty() || v[0]=='('))
    parse_error("Invalid URL value");
}

// Inserts a \allowbreak after dot and slash
void Parser::url_hack(TokenList& L)
{
  TokenList R;
  for(;;) {
    if(L.empty()) { L.swap(R); return; }
    Token T = L.front();
    R.push_back(T);
    L.pop_front();
    if(L.empty()) continue; // no break needed at end.
    if(T.is_slash_token() && L.front().is_slash_token())
      continue; //no break at the start of http://
    if((T.is_slash_token() || T.get_val() == other_t_offset+'.')
       &&bib_ns::allow_break)
      R.push_back(hash_table.allowbreak_token);
  }
}


// Translates \url. If cur_chr !=0, it is rrrt
// \rrrt{foo} is the same as \url{http://www.inria.fr/rrrt/foo.html}
// \url{\rrrt{foo}} is the same as \rrrt{foo}
void Parser::T_url(subtypes c)
{
  bool is_rrrt = c==1;
  leave_v_mode();
  InUrlHandler something;
  InLoadHandler something_else;
  SaveCatcode unused2('~',12);
  SaveCatcode unused3('&',13);
  SaveCatcode unused4('#',13);
  TokenList X = mac_arg();
  if(!X.empty()) {
    Token T = X.front();
    token_from_list(T);
    if(cur_cmd_chr.get_chr()==1 && cur_cmd_chr.get_cmd()==url_cmd) {
      X.pop_front();
      is_rrrt = true;
    }
  }
  if(is_rrrt) {
    Tbuf << bf_reset << "http://www.inria.fr/rrrt/";
    TokenList tmp = Tbuf.str_toks(false); // what about new line here ?
    X.splice(X.begin(),tmp);
    Tbuf << bf_reset << ".html";
    tmp = Tbuf.str_toks(false);
    X.splice(X.end(),tmp);
  }
  TokenList Y;
  bool in_href = the_stack.is_frame2(cst_hanl);
  if(!in_href){
    Y = X;
    brace_me(Y);
  }
  X.push_front(hash_table.urlfont_token);
  brace_me(X);
  if(in_href) {
    url_hack(X);
    T_translate(X);
  }
  else {
    string x = xml_to_string(translate_list(Y));
    url_hack(X);
    Xmlp y = translate_list(X);
    new_xref(y,x,true);
  }
}

// Grabs the text of the URL. This does nothing special with ~.
// Argument is translated in a group.
Xmlp Parser::T_hanl_text()
{ 
  push_level(bt_local);
  Xmlp val = xT_next_arg();
  pop_level(false,bt_local);
  return val;
}

// This fetches the URL.
Xmlp Parser::T_hanl_url()
{
  InUrlHandler something;
  InLoadHandler something_else;
  Xmlp B = xT_next_arg();
  return B;
}

// Translates \href or \htmladdnormallink
// c is 0 for \htmladdnormallink, 2 for \href
void Parser::T_hanl(subtypes c)
{
  leave_v_mode();
  the_stack.push(the_names[cst_hanl],0);
  the_stack.hack_for_hanl();
  Xmlp B, val;
  if(c==2) {
    B = T_hanl_url();
    val = T_hanl_text();
  } else {
    ignore_next_optarg();
    val = T_hanl_text();
    B = T_hanl_url();
  }
  the_stack.pop(cst_hanl);
  int e = main_ns::nb_errs;
  unexpected_seen_hi = false; 
  string b = B->convert_to_string();
  bool failed = e != main_ns::nb_errs;
  if(unexpected_seen_hi && failed)
    main_ns::log_and_tty << "you should perhaps use \\Href{\\url{x}}{y}\n"
			 << "  instead of \\Href{y}{\\url{x}}\n";
  new_xref(val, b, !failed);
}

// This should work, whatever the mode...
// If env is true, we grab the content of the env.

Xmlp Parser::special_tpa_arg(String name,String y, bool par,bool env,bool has_q)
{
  if(!y ||y[0]==0) {
    TokenList L = mac_arg();
    back_input(hash_table.par_token);
    back_input(L);
    return 0;
  }
  mode m = the_stack.get_mode();
  the_stack.set_arg_mode();
  bool has_atts = tpa_buffer.look_at_space(y);
  Istring Y ;
  if(!has_atts)
    Y = Istring(y); // shit...
  else Y = Istring(tpa_buffer.c_str());
  if(par)
    the_stack.set_v_mode();
  the_stack.push(Y,new Xml(Y,0));
  if(has_q) the_stack.mark_omit_cell();
  Xid cid = the_stack.get_top_id();
  if(has_atts) tpa_buffer.push_back_special_att(cid);
  if(par) 
    ileave_v_mode();
  Buffer&B =tpa_buffer;
  if(!env) {
    B << bf_reset << name << "@hook";
    cur_tok = hash_table.locate(B);
    if(!hash_table.eqtb[cur_tok.eqtb_loc()].is_undefined()) {
      Token T = cur_tok;
      TokenList L = mac_arg();
      brace_me(L);
      L.push_front(T);
      brace_me(L);
      back_input(L);
    }
  }
  bool special_case = false;
  if(!env) scan_left_brace();
  push_tpa();
  B << bf_reset << name << "@helper";
  cur_tok = hash_table.locate(B);
  if(!hash_table.eqtb[cur_tok.eqtb_loc()].is_undefined()) {
    back_input(cur_tok);
    if(!env) special_case = true;
  }
  if(special_case) {
    cur_level++;
    if(tracing_stack()) 
      the_log <<lg_startstack << "level + " << cur_level 
	      << " (spec)" << lg_end;
  }
  try {
    translate_all();
  } catch (EndOfData tmp) {};
  flush_buffer();
  if(special_case) {
    if(tracing_stack()) 
      the_log <<lg_startstack << "level - " << cur_level 
	      << " (spec)" << lg_end;
    cur_level--;
  }
  if(par) the_stack.pop(cst_p);
  the_stack.pop(Y);
  the_stack.set_mode(m);
  return the_stack.remove_last();
}

Xmlp Parser::tpa_exec(String cmd)
{
  mode m = the_stack.get_mode();
  the_stack.set_arg_mode(); 
  Istring Y = Istring(cmd);
  the_stack.push(Y,new Xml(Istring(),0)); 
  tpa_buffer << bf_reset << cmd;
  finish_csname(tpa_buffer,cmd);
  get_token();
  TokenList L;
  L.push_back(cur_tok);
  T_translate(L);
  the_stack.pop(Y);
  the_stack.set_mode(m);
  return the_stack.remove_last();
}

// \@reevaluate\foo\bar\gee is \foo\gee\bar\gee
// \@reevaluate\foo\bar{gee} is \foo{gee}\bar{gee}
// 
void Parser::T_reevaluate()
{
  bool in_env = remove_initial_star();
  TokenList L1 = mac_arg();
  TokenList L2 = mac_arg();
  skip_initial_space();
  // check the easy case
  if(!in_env && !cur_cmd_chr.is_open_brace()) {
    back_input(cur_tok);
    back_input(L2);
    back_input(cur_tok);
    back_input(L1);
    return;
  }
  if(in_env) back_input(cur_tok);
  // Now the hard case
  Buffer& B = tpa_buffer;
  B.reset();
  if(!scan_for_eval(B,in_env))
    parse_error("reevaluate has problems with end of input");
  if(in_env) { // unread tokens should be \end{cur}
    get_token(); // the \end token  
    string s = group_to_string(); 
    if(!is_env_on_stack(s)) {
      parse_error("cannot close environment ",s,"bad \\end");
      return;
    }
    pop_level(true,bt_env);
  }
  Tbuf.reset();
  T_reevaluate0(L1,in_env);
  T_reevaluate0(L2,in_env);
  if(tracing_commands())
    the_log << lg_startbrace << "Reeval: " << Tbuf << lg_endbrace;
  push_input_stack("(reevaluate)",false);
  lines.push_front(Clines(-1));
  lines.split_string(Tbuf.c_str(),0);
}

void Parser::T_reevaluate0(TokenList& L1, bool in_env)
{
  Abuf.reset();
  while(!L1.empty()) {
    Abuf << L1.front();
    L1.pop_front();
  }
  if(in_env) Tbuf << "\\begin{" << Abuf << "}" << tpa_buffer
		  << "\\end{" << Abuf << "}%\n";
  else Tbuf << Abuf  << "{" << tpa_buffer << "}%\n";
}


// Translates \uppercase, \MakeUpperCase, etc
void Parser::change_case(int c)
{
  Token T = cur_tok;
  bool to_upper = (c==1 || c==3 || c==5);
  int offset = to_upper ? uc_code_offset : lc_code_offset;
  int k = to_upper ? +1 : -1;
  Token* table = to_upper ? uclc_list : uclc_list+1;
  bool ltx = (c>=2);
  bool extended = c>=4;
  TokenList L = ltx ? mac_arg() : scan_general_text();
  if(ltx) {
    push_level(bt_brace);
    //    if(to_upper) {} // definir \i et \j
    if(extended) {
      eval_let(hash_table.nocase_e_token,  hash_table.nocase_i_token,false);
      eval_let(hash_table.cite_e_token,  hash_table.cite_i_token,false);
      eval_let(hash_table.ref_token,  hash_table.ref_i_token,false);
      eval_let(hash_table.label_token,  hash_table.label_i_token,false);
      eval_let(hash_table.ensuremath_token,  hash_table.ensuremath_i_token,false);
    }
    for(int i=0; i< 11;i++) {
      TokenList L;
      L.push_back(table[2*i+k]);
      new_macro(L,table[2*i]);
    }
    scan_toks_edef(L);
    pop_level(false,bt_brace);
  }
  if(tracing_commands()) 
    the_log << lg_startbrace << T << "(a)->" << L << lg_endbrace;
  token_iterator P = L.begin();
  token_iterator E = L.end();
  TokenList res;
  while (P != E) {
    Token a = *P;
    ++P;
    int bl = 0;
    if(a.is_OB_token()) bl++;
    else if(a.is_CB_token()) bl--;
    if(extended&&bl==0 &&a == hash_table.nocase_e_token) {
      int b = 0;
      if(a!= hash_table.nocase_e_token) res.push_back(a);
      for(;;) {
	if(P==E) break;
	a = *P;
	++P;
	res.push_back(a);
	if(a.is_OB_token()) b++;
	else if(a.is_CB_token()) b--;
	if(b<=0) break ;
      }
      continue;
    }
    if(extended&&bl==0 && a.is_math_shift()) {
      res.push_back(a);
      for(;;) {
	if(P==E) break;
	a = *P;
	++P;
	res.push_back(a);
	if(a.is_math_shift()) break;
      }
      continue;
    } 
    if(a.get_val()<cs_token_flag+single_base) {
      int b = a.chr_val();
      int cx = eqtb_int_table[b+offset].get_val();
      if(cx) {
	res.push_back(Token(a.get_val()-b+cx));
	continue;
      }
    }
    res.push_back(a);
  }
  if(tracing_commands()) 
    the_log << lg_startbrace << T << "->" << res << lg_endbrace;
  back_input(res);
}

// This parses the arguments A,B in \put(A,B){C}
void Parser::T_twoints(TokenList&A, TokenList&B)
{
  Token match (other_t_offset,'(');
  get_token();
  if(cur_tok != match )
    bad_macro_prefix(cur_tok,match);
  A = read_until_long(hash_table.comma_token);
  B = read_until_long(Token(other_t_offset,')'));
}

// Reads the tokens, converts them to dimension.
Istring Parser::dimen_attrib(ScaledInt A)
{
  Buffer& B = the_main->SH.shbuf();
  B.reset();
  B.push_back(A,glue_spec_empty);
  int i = B.length();
  if(i>0&& B[i-1]=='0') { B.remove_last(); i--; }
  if(i>0&& B[i-1]=='.') B.remove_last();
  return Istring(the_main->SH.hash_find());
}

void Parser::back_input_pt(bool spec)
{
  if(spec) { 
    back_input(Token(letter_t_offset,'t'));
    back_input(Token(letter_t_offset,'p'));
  } else
    back_input(hash_table.unitlength_token);
}

ScaledInt Parser::token_list_to_dim(TokenList& a, Token C, bool spec)
{
  back_input_pt(spec);
  back_input(a);
  scan_dimen(false,C);
  return cur_val.get_dim_val();
}
 
// Same code, but calls dimen_attrib
Istring Parser::token_list_to_att(TokenList& a, Token C, bool spec)
{
  back_input_pt(spec);
  back_input(a);
  scan_dimen(false,C);
  return dimen_attrib(cur_val.get_dim_val());
}

void Parser::T_twoints(Istring& A, Istring& B, Token C)
{
  TokenList a;
  TokenList b;
  T_twoints(a,b);
  A = dimen_attrib(ScaledInt(scan_int(a,C)<<16));
  B = dimen_attrib(ScaledInt(scan_int(b,C)<<16));
}

void Parser::T_twodims(Istring& A, Istring& B,Token C)
{
  TokenList a;
  TokenList b;
  T_twoints(a,b);
  A = token_list_to_att(a,C,false);
  B = token_list_to_att(b,C,false);
}

Istring Parser::get_c_val(Token x)
{
  back_input(hash_table.CB_token);
  back_input (x);
  back_input(hash_table.OB_token);
  return nT_next_arg();
}

void Parser::T_bezier(int c)
{
  Token C = cur_tok;
  flush_buffer();
  Istring a1,a2,b1,b2,c1,c2;
  Istring w;
  {
    TokenList L;
    if(c) next_optarg(L);
    else L = mac_arg();
    if(!L.empty()) 
      w = token_list_to_att(L,C,true); // integer....
  }
  T_twodims(a1,a2,C);
  T_twodims(b1,b2,C);
  T_twodims(c1,c2,C);
  AttList& res = the_stack.add_newid0(np_bezier);
  res.push_back(np_repeat,w);
  res.push_back(np_c2,c2);
  res.push_back(np_c1,c1);
  res.push_back(np_b2,b2);
  res.push_back(np_b1,b1);
  res.push_back(np_a2,a2);
  res.push_back(np_a1,a1);
}


// put \line \vector \oval
void Parser::T_put(subtypes c)
{
  Token C = cur_tok;
  flush_buffer();
  Istring A,B,D;
  name_positions x0;
  switch(c) {
  case put_code: x0 = np_put; break;
  case line_code: x0 = np_line; break;
  case vector_code: x0 = np_vector; break;
  case oval_code: x0 = np_oval; break;
  case multiput_code: x0 = np_multiput; break;
  case scaleput_code: x0 = np_scaleput; break;
  case frame_code: x0 = np_frame; break;
  default: x0 = cst_empty;
  }
  bool star = remove_initial_star();
  if(c==frame_code) {
    the_stack.push1(x0); 
    the_stack.set_arg_mode();
    T_next_arg();
    the_stack.pop(x0);  
    return;
  }
  if(c==multiput_code && star) { 
    T_multiput(); 
    remove_initial_space_and_back_input();
    return;
  }
  if(c==put_code || c==oval_code ||c==multiput_code||c==scaleput_code)
    T_twodims(A,B,C);
  else
    T_twoints(A,B,C);
  if(c==oval_code) {
    Istring specs = nT_next_optarg();    
    AttList& val = the_stack.add_newid0(x0);
    val.push_back(np_specs,specs);
    val.push_back(np_ypos,B);
    val.push_back(np_xpos,A);  
    return;
  }
  if(c != put_code && c != multiput_code && c!=scaleput_code) { // line vector
    TokenList L = mac_arg(); 
    D = token_list_to_att(L,C,false);
    AttList& AL= the_stack.add_newid0(x0);
    AL.push_back(np_width,D);
    AL.push_back(np_ydir,B);
    AL.push_back(np_xdir,A);
    return;
  }
  // Case of \put or \multiput \scaleput
  the_stack.push1(x0); 
  the_stack.set_arg_mode();
  Xid cur_id = the_stack.get_top_id();
  if(c==scaleput_code) {
    AttList& AL= cur_id.get_att();
    AL.push_back(np_yscalex,get_c_val(hash_table.yscalex_token));
    AL.push_back(np_xscaley,get_c_val(hash_table.xscaley_token));
    AL.push_back(np_yscale,get_c_val(hash_table.yscale_token));
    AL.push_back(np_xscale,get_c_val(hash_table.xscale_token));
  }
  if(c==multiput_code) {
    Istring aa,bb;
    T_twodims(aa,bb,C);
    cur_id.add_attribute(np_dy,bb);
    cur_id.add_attribute(np_dx,aa);
    TokenList L = mac_arg(); 
    D = token_list_to_att(L,C,true); // integer....
    cur_id.add_attribute(np_repeat,D); 
  }
  cur_id.add_attribute(np_ypos,B);
  cur_id.add_attribute(np_xpos,A);
  T_next_arg();
  the_stack.pop(x0);  
  if(c==put_code|| c==multiput_code) remove_initial_space_and_back_input();
}

void Parser::T_linethickness(int c)
{
  flush_buffer();
  name_positions C = np_line_thickness;
  if(c==thicklines_code) C = np_thick_lines;
  if(c==thinlines_code) C = np_thin_lines;
  AttList& res = the_stack.add_newid0(C);
  if(c==linethickness_code)
    res.push_back(np_size,nT_next_arg());
}

void Parser::T_curves(int c)
{
  Token C = cur_tok;
  flush_buffer();
  name_positions x0 = cst_empty; 
  switch(c) {
  case arc_code: x0 = np_arc; break;
  case bigcircle_code: x0 = np_bigcircle; break;
  case closecurve_code: x0 = np_closecurve;  break;
  case curve_code: x0 = np_curve; break;
  case tagcurve_code: x0 = np_tagcurve; break;
  }
  the_stack.push1(x0); 
  the_stack.set_arg_mode();
  Xid cur_id = the_stack.get_top_id();
  Istring specs = nT_next_optarg();
  if(!specs.null()) cur_id.add_attribute(np_curve_nbpts,specs);
  TokenList emptyl;
  cur_id.add_attribute(np_unit_length,token_list_to_att(emptyl,C,false));
  if(c==arc_code) {
    Istring aa,bb;
    T_twodims(aa,bb,C);
    cur_id.add_attribute(np_ypos,bb);
    cur_id.add_attribute(np_xpos,aa);
    specs = nT_next_arg();
    cur_id.add_attribute(np_angle,specs);
  } else if(c==bigcircle_code) {
    cur_id.add_attribute(np_size,nT_next_arg());
  } else {
    Token match (other_t_offset,'(');
    skip_initial_space();
    if(cur_tok != match )
      bad_macro_prefix(cur_tok,match);
    TokenList L = read_until_long(Token(other_t_offset,')'));
    T_translate(L);
  }
  the_stack.pop(x0);  
}


void Parser::T_multiput() 
{
  Token C = cur_tok;
  TokenList xpos,ypos;
  T_twoints(xpos,ypos);
  ScaledInt X,Y,Dx,Dy;
  X = token_list_to_dim(xpos,C,false);
  Y = token_list_to_dim(ypos,C,false);
  TokenList dx,dy;
  T_twoints(dx,dy);
  Dx = token_list_to_dim(dx,C,false);
  Dy = token_list_to_dim(dy,C,false);
  TokenList w = mac_arg();
  back_input(w);
  int r = scan_int(C);
  TokenList L = mac_arg();
  brace_me(L);
  while(r>0) {
    TokenList Lc = L;
    back_input(Lc);
    the_stack.push1(np_put); 
    the_stack.set_arg_mode();
    AttList& AL = last_att_list();
    AL.push_back(np_ypos,dimen_attrib(Y));
    AL.push_back(np_xpos,dimen_attrib(X));
    T_next_arg();
    the_stack.pop(np_put);  
    the_stack.add_nl();
    r--;
    X += Dx;
    Y += Dy;
  }
}

void Parser::T_dashline(subtypes c)
{
  Token T= cur_tok;
  flush_buffer();
  name_positions x0 = cst_empty;
  if(c==dashline_code) x0 = np_dashline;
  if(c==drawline_code) x0 = np_drawline;
  if(c==dottedline_code) x0 = np_dottedline;
  if(c==circle_code) x0 = np_circle;
  if(c==circle_code) { // circle
    bool has_star = remove_initial_star();
    TokenList L = mac_arg();
    Istring aa = token_list_to_att(L,T,false);
    AttList &AL = the_stack.add_newid0(x0);
    AL.push_back(np_size,aa);
    if(has_star) AL.push_back(np_full,np_true);
    return;
  }
  Istring A = nT_next_optarg();
  Istring B;
  if(c==dashline_code || c==dottedline_code) B = nT_next_arg();
  Istring C = nT_next_optarg();
  the_stack.push1(x0); 
  the_stack.set_arg_mode();
  AttList& AL = last_att_list();
  AL.push_back(np_arg1,A);
  AL.push_back(np_arg2,B);
  AL.push_back(np_arg3,C);
  for(;;) {
    Istring xpos,ypos;  
    skip_initial_space();
    if(cur_tok.is_valid()) back_input();
    if(!cur_tok.is_open_paren()) break;
    T_twodims(xpos,ypos,T);
    AttList& res = the_stack.add_newid0(np_point);
    res.push_back(np_ypos,ypos);
    res.push_back(np_xpos,xpos);
  }
  the_stack.pop(x0);
}

// This is called before we output a character
void Parser::LC()
{
  if(the_stack.in_v_mode()) ileave_v_mode();
  if(the_stack.in_no_mode() || the_stack.in_bib_mode()) {
    signal_error("Text found in a mode where no text is allowed");
    if(the_stack.is_frame(np_gloss)) {
      main_ns::log_and_tty << "Maybe \\glo is missing?\nHope for the best\n";
      the_stack.set_arg_mode();
      return;
    }
    if(the_stack.in_bib_mode()) return;
    the_stack.set_mode(mode_argument);
    the_log << "Bad mode:XML stack is: ";
    the_stack.trace_stack();
    the_log << "\n";

  }
}

void Parser::T_error()
{
  if(cur_cmd_chr.get_chr()==1) {
    parse_error("Can be used only in preamble: ",cur_tok,"","noprerr");
    return;
  }
  flush_buffer();
  string b = sT_next_arg(); // msg
  err_ns::local_buf << bf_reset << "Error: " << b;
  err_ns::signal_error(b.c_str(),0);
}

// scans an element id, in brackets, default is cur_id
int Parser::read_elt_id(Token T)
{
  int cur= the_stack.cur_xid().value;
  int upper=the_stack.get_xid().value;
  int n = scan_special_int_d(T,cur);
  if(n>0 && n<=upper) return n;
  err_ns::local_buf << bf_reset << "Bad xml id replaced by 0: " << n;
  err_ns::signal_error("number too big",0);
  return 0;
}

// Adds an attribute pair to some element
void Parser::T_xmladdatt(subtypes c)
{
  Token T = cur_tok;
  bool force = remove_initial_star();
  flush_buffer();
  int n = 0; 
  InLoadHandler something_else;

  if(c==addatt_to_cur_code) n = the_stack.cur_xid().value;
  else if(c==addatt_to_last_code) n = the_stack.get_xid().value;
  else if(c==addatt_to_doc_code) n = 1;
  else if(c==addatt_to_index_code) n= get_index_value();
  else n = read_elt_id(T);
  cur_tok = T;
  Istring key = nT_next_arg();
  cur_tok = T;
  Istring val = nT_next_arg();
  if(key.empty()) {
    if(!force) return;
    Xml* e = the_stack.elt_from_id(n);
    if(!e) return;
    e->change_name(val);
    return;
  }
  if(n) Xid(n).add_attribute(key,val,force);
}

// Returns the value of an attribute or element
string Parser::get_attval()
{ 
  Token T = cur_tok;
  flush_buffer();
  int n = read_elt_id(T);
  Istring key = nT_next_arg();
  if(key.empty()) {
    Xml* e = the_stack.elt_from_id(n);
    if(!e) return ""; 
    return e->get_name().c_str();
  }
  Istring res = Xid(n).has_attribute(key);
  return res.c_str();
}


void Parser::T_define_verbatim_env()
{
  string a = group_to_string();
  string b = group_to_string();
  Token t1 = find_env_token(a,true);
  Token t2 = find_env_token(b,true);
  eval_let(t1,t2,true); // global assignments OK ?
  Token xt1 = find_env_token(a,false);
  Token xt2 = find_env_token(b,false);
  eval_let(xt1,xt2,true);
  TokenList L = mac_arg();
  string t3 = a+"@hook";
  make_token(t3.c_str());
  TL.pop_front();
  new_macro(L,cur_tok);
}

// Implements some commands (\ignorespaces, \mark, \penalty, \@@end,
// \message and \errmessage
void Parser::T_specimp(int c)
{
  switch(c) {
  case ignorespaces_code:
    remove_initial_space_and_back_input();
    return;
  case mark_code:
    expand_mark(subtypes(c));
    return;
  case marks_code:
    scan_int(cur_tok);
    expand_mark(subtypes(c));
    return;
  case penalty_code:
    scan_int(cur_tok);
    return;
  case abort_code:
    close_all(); tralics_ns::close_file(main_ns::log_and_tty.L.fp);
    exit(0);
  case sleep_code: 
    txsleep(scan_int(cur_tok));
    return; 
  case prompt_code: {
    string S =tex_write(-1);
    readline_newprompt(S);
    return;
  }
  case atatend_code:
    cout << "Terminated with " << cur_tok  << " at line  " << get_cur_line()<<"\n";
    close_all();
    return;
  case message_code:
    tex_message(tex_write(-1));
    return;
  case errmessage_code:
    err_ns::local_buf << bf_reset << tex_write(-1);;
    err_ns::signal_error("",1);
    return;
  case discretionary_code: 
    scan_left_brace_and_bi();
    ignore_next_arg();
    scan_left_brace_and_bi();
    ignore_next_arg();
    scan_left_brace_and_bi();
  return;
  case allowbreak_code:
    flush_buffer();
    the_stack.add_newid0(np_allowbreak);
    return;
  }
}

// Commands that do nothing, just print a message.
void Parser::T_unimp(subtypes c)
{
  parse_error("Unimplemented command ",cur_tok,"","unimplemented");

  switch(c) {
  case accent_code:
    extended_chars(scan_27bit_int());
    return;
  case delimiter_code:
    scan_twenty_seven_bit_int();
    return;
  case insert_code:
    scan_eight_bit_int();
    scan_left_brace_and_bi();
    return;
  case vadjust_code:
    scan_left_brace_and_bi();
    return;
  case mathaccent_code:
  case mathchar_code:
    scan_fifteen_bit_int();
    return;
  case radical_code:
    scan_27bit_int();
    return;
  case vcenter_code:
    begin_box(513,vbox_code);
    return;
  default:
    return;
 }
}

void Parser::need_bib_mode()
{
  if(the_stack.in_bib_mode())
    return;
  Tbuf << bf_reset << "Command " << name_for_error 
       << " should occur in bibliographic mode only";
  parse_error(Tbuf.to_string());
}
void Parser::need_array_mode()
{
  if(the_stack.in_array_mode())
    return;
  parse_error("Current command should occur in tables only");
}

// \mathversion{bold} is the same as \@mathversion1\relax
// \mathversion{otherwise} is the same as \@mathversion0\relax
void Parser::mathversion()
{
  TokenList arg = mac_arg();
  Buffer&B = Tbuf;
  Tbuf.reset();
  if(!csname_aux(arg,false,B)) return;
  TokenList L;
  Token T = Token(other_t_offset,B.to_string()=="bold" ? '1' : '0');
  L.push_back(hash_table.mathversion_token);
  L.push_back(T);
  L.push_back(hash_table.relax_token);
  back_input(L);
}
