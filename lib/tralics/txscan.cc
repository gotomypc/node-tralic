// Tralics, a LaTeX to XML translator.
// Copyright (C) INRIA/apics/marelle (Jose' Grimm) 2003, 2004-2011

// This software is governed by the CeCILL license under French law and
// abiding by the rules of distribution of free software.  You can  use, 
// modify and/ or redistribute the software under the terms of the CeCILL
// license as circulated by CEA, CNRS and INRIA at the following URL
// "http://www.cecill.info". 
// (See the file COPYING in the main directory for details)

// This file contains the TeX scanner of tralics

#include "tralics.h"
const char* txscan_rcsid=
  "$Id: txscan.C,v 2.95 2011/06/28 08:15:25 grimm Exp $";

namespace{
  Buffer file_name_buffer; // a buffer for reading file names
  Buffer buffer_for_thetoks; // a buffer for the_toks()
  Buffer scratch; // See insert_string
  TexFonts tfonts; // the font table
  TexOutStream tex_out_stream; // the output streams
  vector <InputStack*> cur_input_stack; // the input streams
  bool name_in_progress = false; // see the source of TeX
  FileForInput tex_input_files[nb_input_channels]; // the input files
  bool scan_glue_opt = false; // true if optional glue as found
  vector<int>penalties[4];
  vector<ScaledInt> parshape_vector;
  bool seen_eof=true; // true if every_eof has been inserted for the current file
  Buffer local_buf;  // a local buffer
}

extern void read_xml(string);

namespace translate_ns {
  void tex_message(String s);
}

namespace scan_ns{
  void mu_error();
}

namespace buffer_ns {
  int current_escape_char();
  int char_to_dig(Utf8Char c);
};

namespace io_ns {
  void set_enc_param(int enc,int pos,int v);
  int get_enc_param(int enc,int pos);
  String dump_it(internal_type s);
}


extern void readline(char*buffer,int screen_size);
extern void set_math_char(uchar c, int f,string s);
extern string get_math_char(uchar c, int f);



// Section 1: output streams.

// When tralics starts, everything is closed.
// For i=16, 17 and 18, the channel is always closed.
TexOutStream::TexOutStream()
{
  for(int i=0;i<nb_input_channels;i++) write_file[i] = 0;
  for(int i=0;i<nb_output_channels;i++) write_open[i] = false;
}

// This closes an output channel. Assumes chan>=0, and chan<= 18
void TexOutStream::close(int chan)
{
  if(chan<0 || chan>=nb_real_output_channels) return; // this cannot happen
  if(write_open[chan]) {
    fstream *F = write_file[chan];
    F->close();
    delete F;
    write_file[chan] = 0;
    write_open[chan] = false;
  }
}

// This opens an output channel. Assumes chan>=0, and chan<= 18
// What if the file cannot be opened ?
void TexOutStream::open(int chan, string file_name)
{
  if(chan<0 || chan>=nb_real_output_channels) return; // This cannot happen
  close(chan);
  String fn= tralics_ns::get_out_dir(file_name);
  fstream* fp = new fstream(fn,std::ios::out);
  if(!fp) return; // no error ?
  if(!*fp) return; // no error ?
  write_file[chan] = fp;    
  write_open[chan] = true;
}

// This gets the object to \write in local_buf.
// In the case of \write18, the string is printed  on the log file
// Otherwise, a newline is added, and the caller will print the string
// In the case of \message, we have chan=-1. No \n is added
String Parser::tex_write(int chan)
{
  TokenList L = scan_general_text();
  scan_toks_edef(L);
  Buffer& B = local_buf;
  B << bf_reset << L;
  if(chan==write18_slot) {
    the_log << "\\write18=" << B.convert_to_log_encoding() << "\n";
  } else if(chan>=0)
    B << "\n";
  return B.convert_to_log_encoding();
}

// Implements \typeout, \wlog
// it is \immediate\write17 in LaTeX
// Is redefined later to take an argument
void Parser::typeout(int c)
{
  if(c==0)
    main_ns::log_and_tty << tex_write(negative_out_slot);
  else 
    the_log << tex_write(negative_out_slot);
}


// This implements \write, \openout, and \closeout
// action is \immediate, since there is no shipout routine
void Parser::tex_extension(int cc)
{
  int chan = 0;
  Token T = cur_tok;
  if(cc==openout_code)
    chan = scan_int(cur_tok,max_openout,"output channel number");
  else {
    chan = scan_int(cur_tok); 
    if(chan<0) chan = negative_out_slot;
    else if (chan>max_openout && chan != write18_slot)
      chan = positive_out_slot;
    if(chan==write18_slot && !the_main->is_shell_escape_allowed())
      chan = positive_out_slot;
  }
  if(cc==openout_code) { 
    scan_optional_equals(); 
    string file_name = scan_file_name();
    tex_out_stream.open(chan,file_name);
  } else if(cc==write_code) {
    cur_tok = T;
    String s = tex_write(chan);
    if(chan==write18_slot) {
      system(s);
    } else if(tex_out_stream.is_open(chan)) 
      tex_out_stream.write(chan,s); 
    else if (chan==negative_out_slot) the_log << s;
    else main_ns::log_and_tty << s;
  } else // cc==close_code
    tex_out_stream.close(chan);
}

// Section 2. input file handling

// This closes a file opened by \openin
void FileForInput::close()
{
  if(is_open) {
    is_open = false;
    the_lines.before_close(false);
    the_lines.clear();
  }
}

// Open a file for \openin. if action is false, the file does not exist
// No on-the fly conversion here
void FileForInput::open(string file, bool action)
{
  if(!action) {
    the_log <<lg_start << "++ Cannot open file " << file << " for input\n";
  } else {
    tralics_ns::read_a_file(the_lines,main_ns::path_buffer.to_string(),1);
    is_open = true;
    cur_line.reset();
    line_no = 0;
    the_lines.after_open();
  }
}

// This puts in cur_input_stack a new slot, containing the current state
// of affairs. It kills the input buffer and the current line.
void Parser::push_input_stack(const string& name,bool restore_at)
{
  InputStack* W= new InputStack(name,get_cur_line(),state, cur_file_pos,seen_eof);
  seen_eof = true; // somebody will set it to false some day
  cur_input_stack.push_back(W); 
  int n = cur_input_stack.size();
  if(tracing_io()) {
    the_log << lg_start << "++ "
	    << "Input stack ++ " << n << " " << W->get_name()<< "\n";
  }
  W->set_line_ptr(lines);
  W->set_line_pos(input_line_pos);
  W->set_line_vector(input_line);
  W->get_TL().swap(TL);
  if(restore_at) {
    W->set_at_val(eqtb_int_table[uchar('@')].get_val());
    eqtb_int_table[uchar('@')].set_val(11);
    the_log << lg_start << "++ Made @ a letter\n";
  }
  input_line.clear();
  lines.set_interactive(false);
  input_line_pos=0;
  cur_file_pos = 0;
}

// Undoes push_input_stack.
// If vb is false, we are reading from a Token list or something like that.
// Otherwise, we read from a real file, and indicate that it is closed.
void Parser::pop_input_stack(bool vb)
{
  int n = cur_input_stack.size();
  if(n==0) {
    if(tracing_io()) {
      the_log << lg_start << "++ Input stack empty \n";
    }
    return;
  }
  InputStack*W = cur_input_stack.back();
  if(vb) 
    lines.before_close(true);
  set_cur_line(W->get_line_no());
  state = W->get_state();
  W->get_line_ptr(lines);
  int at =  W->get_at_val();
  if(at>=0) {
    eqtb_int_table[uchar('@')].set_val(at);
    if(tracing_io())
      the_log << lg_start << "++ Catcode of @ restored to " << at << "\n";
  }
  input_line.clear();
  input_line.insert(input_line.end(), W->get_B().begin(), W->get_B().end());
  input_line_pos = W->get_line_pos();
  back_input(W->get_TL());
  if(cur_file_pos) insert_hook(cur_file_pos);
  cur_file_pos = W->get_file_type();
  the_log << lg_start << "++ cur_file_pos restored to " << cur_file_pos << "\n";
  cur_input_stack.pop_back();
  if(tracing_io()) {
    the_log << lg_start << "++ "
	    << "Input stack -- " << n << " " << W->get_name() << "\n";
  }
  seen_eof = W->get_seen_eof();
  delete W;
}

// Make sure no character can be obtained from the stream
void InputStack::destroy()
{
  TL.clear();
  B.clear();
  L.clear();
}

// This kills all pending input
void Parser::close_all()
{
  if(tracing_io())  the_log << lg_start << "++ close all files\n";
  while (cur_input_stack.size())
    pop_input_stack(true);
  TL.clear();
  input_line.clear();
  input_line_pos = 0;
  lines.clear();
  lines.set_interactive(false);
}

// A file name is a special thing in TeX. 
// We read until we find a non-char, or a space. 
string Parser::scan_file_name()
{
  if(name_in_progress) return "sabotage!"; // recursion killer.
  name_in_progress = true;
  remove_initial_space_and_back_input();
  Buffer& B = file_name_buffer;
  B.reset();
  for(;;) {
    if(get_x_token()) break;
    if(cur_cmd_chr.is_letter_other())
      B.push_back(cur_cmd_chr.char_val());
    else if(cur_cmd_chr.get_cmd()==underscore_catcode) // allow foo_bar
      B.push_back(cur_cmd_chr.char_val());
    else if(cur_cmd_chr.is_space()) break;
    else { back_input(); break; }
  }
  name_in_progress = false;
  return B.to_string();
}

// This implements  \input, \endinput, \scantokens
// This is called from expand. We have to print the command name in the log 
// file. The real code of input is in T_input().
void Parser::x_input(int q)
{
  if(q==endinput_code) {
    if(tracing_commands()) the_log.dump("endinput");
    force_eof = true;
    return;
  }
  if(q==end_all_input_code) {
    if(tracing_commands()) the_log.dump("endallinput");
    int n = cur_input_stack.size();
    for(int i=0;i<n;i++) cur_input_stack[i]->destroy();
    force_eof = true;
    return;
  }
  if(q==scantokens_code) {
    if(tracing_commands()) the_log.dump("scantokens");
    static Buffer B;
    TokenList L = scan_general_text();
    B.reset();
    B << L;
    push_input_stack("(scantokens)",false);
    lines.split_string(B.c_str(),0);
    lines.after_open();
    seen_eof = false;
    return;
  }
  if(name_in_progress) {
    insert_relax();
    if(tracing_commands()) 
      the_log << lg_start << "{insert \\relax for \\input}" << lg_end;
    return;
  }
  if(tracing_commands()) the_log.dump("input");
  T_input(q);
}

// This implements \ifeof
bool Parser::is_in_open()
{
  int ch = scan_int(cur_tok,15,"input channel number");
  return !tex_input_files[ch].is_valid();
}

// In TeX, you say \input foo, in latex you say \input{foo}
// This accepts both syntaxes.
string Parser::latex_input(int q)
{
  remove_initial_space_and_back_input();
  string file;
  if(q==include_code || cur_tok.is_OB_token()) { 
    flush_buffer(); 
    if(!cur_tok.is_OB_token()) {
      parse_error("Missing {} for ",name_for_error,"","Missing brace");
      file = ".tex";
    } else {
      InLoadHandler somthing;
      file = sT_next_arg();
    }
  } else file = scan_file_name();
  if(tracing_commands()) 
    the_log << lg_startbracebs << "input " << file << lg_endbrace;
  return file;
}

// This implements \IfFileExists, \InputIfFileExists,
//  \input, \openin, \closein
void Parser::T_input(int q)
{
  int stream = 0;
  bool seen_star = false;
  bool seen_plus = false;
  if(q==openin_code||q==closein_code) {
    stream = scan_int(cur_tok,max_openin,"input channel number");
    tex_input_files[stream].close();
  }
  if(q==closein_code)
    return;
  string file;
  if(q==Input_code || q== include_code || q==inputifexists_code)
    seen_star = remove_initial_star();
  if(q==inputifexists_code || q==ifexists_code)
    seen_plus = remove_initial_plus(true);
  if(q==Input_code) q = include_code;
  if(q==openin_code) {
    scan_optional_equals();
    file = scan_file_name();
  } else if(q==input_code || q==include_code) file = latex_input(q);
  else { 
    flush_buffer(); 
    InLoadHandler somthing;
    file = sT_next_arg(); 
  }
  if(q==scantokens_code+1) {
    read_xml(file); return;
  }
  bool res = false;
  if(seen_plus) res = tralics_ns::find_no_path(file);
  else {
    res = tralics_ns::find_in_path(file);
    if(!res) {
      Buffer&B = local_buf;
      B << bf_reset << file;
      if(!B.is_at_end(".tex")) {
	B.push_back(".tex");
	string F = B.to_string();
	res = tralics_ns::find_in_path(F);
      }
    }
  }
  if(q==openin_code) {
    tex_input_files[stream].open(file,res);
    return;
  }
  if(q==ifexists_code) { one_of_two(res); return; }
  if(q==inputifexists_code) {
    TokenList A = mac_arg();
    TokenList B = mac_arg();
    if(res) open_tex_file(seen_star);
    if(A.empty () && B.empty()) return; // optimise
    if(tracing_commands())
      the_log << lg_start << "++ " << (res ? "iftrue" : "iffalse") << "{"
	      << A << "}{" << B << "}\n";
    one_of_two(A,B,res);
    return;
  }
  if(!res) {
    parse_error("Cannot open input file ", file, "cannot open input");
    return;
  }
  open_tex_file(seen_star);
}


// On-the-fly conversion allowed
void Parser::open_tex_file(bool seen_star)
{
  string file = main_ns::path_buffer.to_string();
  push_input_stack(file,seen_star);
  tralics_ns::read_a_file(lines,file,2);
  lines.after_open();
  seen_eof = false;
}

// Saves the current Token list, ready to read from another one.
void Parser::save_the_state(SaveState& x)
{
  x.copy_and_reset(TL);
  x.set_restricted(restricted);
}

// Undoes a previous save_the_state.
void Parser::restore_the_state (SaveState& x)
{
  TL.clear();
  x.restore(TL);
  restricted = x.get_restricted();
}


// Converts a Hex char (lower case) to digit.
// Returns -1 if impossible.
int buffer_ns::char_to_dig(Utf8Char c)
{
  if(c.is_digit()) return c.val_as_digit();
  else if(c.is_hex()) return c.val_as_hex();
  else return -1; 
}


// Handles the case of ^^^^abcd. One char has been read.
// Returns true in case of success. 
// Puts the character read at location of d
bool Parser::four_hats(Utf8Char cc)
{
  int sz = input_line.size();
  int p = input_line_pos;
  int w = sz-p +1; // 3,4,8 are OK
  if(w>=8 && input_line[p]==cc && input_line[p+1]==cc && input_line[p+2]==cc) {
    int hc1 = buffer_ns::char_to_dig(input_line[p+3]);
    int hc2 = buffer_ns::char_to_dig(input_line[p+4]);
    int hc3 = buffer_ns::char_to_dig(input_line[p+5]);
    int hc4 = buffer_ns::char_to_dig(input_line[p+6]);
    if(hc1>=0 && hc2>=0 && hc3>=0 &&hc4>=0) {
      input_line_pos = p+6;
      input_line[p+6] = Utf8Char((hc1<<12) +(hc2<<8) +(hc3<<4) + hc4);
      return true;
    }
  }
  if(w>=3 && input_line[p]==cc) {
    int hc1 = buffer_ns::char_to_dig(input_line[p+1]);
    int hc2 = buffer_ns::char_to_dig(input_line[p+2]);
    if(hc1>=0 && hc2>=0) {
      input_line_pos = p+2;
      input_line[input_line_pos] = Utf8Char(16*hc1+hc2);
      return true;
    }
  }
  if(w>=2 && input_line[p]==cc) {
    Utf8Char C = input_line[p+1];
    if(!C.is_ascii()) return false;
    uchar c1 = C.ascii_value();
    input_line_pos ++;
    input_line[p+1] = Utf8Char(c1<64 ? c1+64 : c1-64);
    return true;
  }
  return false;
}

// This constructs a command name. Returns the hashtab location of the command
// Reading is done from the input buffer.
// If it is empty, then the null_cs is returned.
Token Parser::get_macro ()
{
  if(at_eol()) return Token(null_cs+cs_token_flag);
  state = state_S;
  Utf8Char c = input_line[input_line_pos++];
  int C = fast_catcode(c.get_value());
  if(C==letter_catcode) {
    mac_buffer.reset();
    mac_buffer.push_back(c);
    for(;;) {
      if(at_eol()) break;
      c = input_line[input_line_pos++];
      C = fast_catcode(c.get_value());
      if(C==letter_catcode) {
	mac_buffer.push_back(c);
	continue;
      }
      if(C==hat_catcode) {
	if(four_hats(c)) continue;
      }
      --input_line_pos;
      break;
    }
    return hash_table.locate(mac_buffer);
  }
  else if(C==space_catcode) return Token(c);
  else if(C==hat_catcode) {
    if(four_hats(c)) return get_macro();
    state = state_M;
    return Token(c); 
  }
  else { state = state_M; return Token(c); }
}

// This constructs a new token by reading characters from the buffer.
// Returns true in case of problem. Either because we are at EOL
// or because a line ended (% or \n), or because a character was ignored
// (space or invalid chars).
bool Parser::next_from_line0 ()
{
  if(at_eol()) return true;
  Utf8Char c = input_line[input_line_pos++];
  cur_cmd_chr = CmdChr(fast_catcode(c.get_value()), subtypes(c.get_value()));
  switch(cur_cmd_chr.get_cmd()) {
  case escape_catcode: {
    cur_tok = get_macro();
    look_at_mac();
    if(cur_cmd_chr.get_cmd()>=usero_cmd) check_outer_validity();
    return false;
  }
  case active_catcode:
    cur_tok.from_cs(c.get_value()+active_base);
    look_at_mac();
    if(cur_cmd_chr.get_cmd()>=usero_cmd) check_outer_validity();
    state = state_M;
    return false;

  case space_catcode: 
    if(state==state_M) { 
      state = state_S; 
      cur_cmd_chr = CmdChr(space_catcode,subtypes(' '));
      cur_tok = hash_table.space_token;
      return false; 
    }
    return true;
  case comment_catcode:
    kill_line();
    return true;
  case eol_catcode: {
    states s = state;
    kill_line();
    if(s==state_N) {
      cur_tok = hash_table.par_token;
      look_at_mac();
      if(cur_cmd_chr.get_cmd()>=usero_cmd) check_outer_validity();
      return false;
    }
    if(s==state_M) {
      // this is different from TeX
      cur_cmd_chr = CmdChr(space_catcode, subtypes('\n'));
      cur_tok = hash_table.newline_token;
      return false;
    }
    return true;
  }
  case hat_catcode:
    if(four_hats(c)) return true;  // cur char has changed
    state = state_M;
    cur_tok.from_cmd_chr(cur_cmd_chr);
    return false;
  case special_catcode: // case of ShortVerb.
    verb_saved_char = c;
    cur_tok = hash_table.verb_token;
    look_at_mac();
    state = state_M;
    return false;
  case ignored_catcode: return true; // ignored
  case invalid_catcode: return true; // invalid
  default:
    state = state_M;
    cur_tok.from_cmd_chr(cur_cmd_chr);
    return false;
  }
}

// This is like above, but we try again. Note that get_a_new_line
// may fail, or fill the buffer, or put something in TL.
bool Parser::next_from_line ()
{
  for(;;) {
    bool res = next_from_line0();
    if(!res) return false;
    if(!at_eol()) continue;
    if(get_a_new_line()) // no other line available, abort
      break;
    if(!TL.empty()) { 
      next_from_list(); 
      return false; 
    }
  }
  cur_tok = invalid_token;
  look_at_mac();
  return true; 
}

// Gets a token from the list. Returns nothing since the list is not empty.
// Handles \noexpand\foo (this is a bit complicated)
void Parser::next_from_list()
{
  Token t = TL.front(); 
  TL.pop_front();
  cur_tok = t;
  if(t.not_a_cmd()) see_non_mac_token(t);
  else {
    look_at_mac();
    if(cur_cmd_chr.get_cmd() == dont_expand_cmd) {
      if(TL.empty()) return; // should not happen....
      t = TL.front(); TL.pop_front();
      cur_tok = t;
      if(t.not_a_cmd()) return; // should not happen....
      look_at_mac();
      if(cur_cmd_chr.is_expandable())
	cur_cmd_chr = CmdChr(relax_cmd, subtypes(special_relax));
    } else if(cur_cmd_chr.get_cmd()>=usero_cmd) check_outer_validity();
  }
}

// The next function reads a character, and puts it in the buffer.
// If the in_env arg is false, stops when seeing a right brace, 
// otherwise \end{foo}
// Return true if end condition is reached false otherwise.

bool Parser::scan_for_eval(Buffer&B, bool in_env)
{
  int b = 0; // brace level
  int elevel = 0; // 
  for(;;) {
    // First read tokens from TL
    if(!TL.empty()) {
      Token t = TL.front(); TL.pop_front();
      if(in_env && b==0) {
	if(t== hash_table.end_token) elevel--;
	if(t== hash_table.begin_token) elevel++;
      }
      if(in_env && b==0 && elevel==-1) {
	back_input(t);
	return true;
      }
      b += B.push_back(t,true);
      if(!in_env &&b==-1) {
	B.remove_last();
	return true;
      }
      continue;
    }
    if(restricted) return false;
    // Now try characters from the input channel
    if(at_eol()) {
      if(get_a_new_line()) // no other line available, abort
	return false;
      continue; // this may set TL
    }
    Utf8Char c = input_line[input_line_pos++];
    if(c=='\r') c = '\n';
    B.push_back(c);
    if(c=='{') b++;
    if(c=='}') {
      b--;
      if(b==-1 && !in_env) {
 	B.remove_last();
	return true;
      }
      if(b==-1) return false;
    }
    if(!(c== '\\')) continue;
    if(at_eol()) continue;
    c = input_line[input_line_pos++];
    B.push_back(c);  // \{ and \} and \\ are normal chars
    if(!in_env) continue;
    if(b != 0) continue;
    if(c=='b') {
      uint p = input_line_pos;
      if(p+3<input_line.size() &&input_line[p]=='e' &&
	 input_line[p+1]=='g' &&
	 input_line[p+2]=='i' &&
	 input_line[p+3]=='n' &&
	 (p+4 ==input_line.size() // this is silly
	  || !input_line[p+4].is_letter()))
	elevel ++;
    }
    if(c=='e') {
      uint p = input_line_pos;
      if(p+1<input_line.size() && input_line[p]=='n' 
	 &&input_line[p+1]=='d' &&
	 (p+2 ==input_line.size() // this is silly
	  || !input_line[p+2].is_letter())) {
	elevel--;
	if(elevel ==-1) {
	  input_line_pos -= 2; // reread backshash e
	  B.remove_last(); // remove these two chars from the buffer
	  B.remove_last();
	  return true;
	}
      }
    }
  }
  return false;
}


// case of \if AB or \ifcat AB. Nasty hack with \noexpand
void Parser::get_x_token_or_active_char()
{
  get_x_token();
  if(cur_cmd_chr.is_relax())
    if (cur_cmd_chr.get_chr()==special_relax) {
      cur_cmd_chr = CmdChr(active_catcode, subtypes(cur_tok.eqtb_loc()));
    }
  if (cur_cmd_chr.get_cmd()>13 || cur_cmd_chr.get_chr()>= int(nb_characters)) 
    cur_cmd_chr = CmdChr(relax_cmd, relax_code);
}

// Same as above, but without hacks for \noexpand
void Parser::token_from_list(Token t)
{
  cur_tok = t;
  if(t.not_a_cmd())  see_non_mac_token(t);
  else look_at_mac();
}

// Returns next token: cur_cmd, cur_chr, cur_tok.
// If TL is not empty, reads from TL, 
// if restricted is true, returns an invalid token if TL is empty
bool Parser::get_token ()
{
  if(!TL.empty()) { 
    next_from_list(); 
    return false; 
  } 
  if(restricted) {  
    cur_tok = invalid_token;
    look_at_mac();
    return true; 
  }
  return next_from_line();
}

// We have to write now the function that reads a line from a file.
// This function is used as an auxiliary.
// Puts a line of input in the buffer; 
// Removes spaces at end of line.
void Buffer::insert_string(const Buffer& s)
{
  wptr = 0;
  int n = s.wptr;
  alloc(n+5); // make sure it is big enough
  ptr = 0;
  int k = 0;
  for(int j =0; j<n; j++) {
    char c = s.buf[j];
    if(c != '\n' && c != '\r') buf[k++] = c;
  }
  while(k>0 && buf[k-1]== ' ') 
    k--;
  buf[k] = 0;
  wptr = k;
}

// This piece of code is executed when a new line, numbered N, is in
// the buffer scratch. We copy it into input_buffer. We extract characters
// and put them in input_line. In case vb is true,
// we print the line in the log file. 
void Parser::store_new_line(int n, bool vb)
{
  set_cur_line(n);
  input_buffer.insert_string(scratch);
  input_buffer.extract_chars(input_line);
  input_buffer.reset_ptr();
  if(vb)
    the_log << lg_start  << "[" << n << "] " 
	    << input_buffer.convert_to_log_encoding() << "\n";
}

void Parser::insert_endline_char()
{
  input_line_pos =0; 
  int cc = eqtb_int_table[endlinechar_code].get_val();
  if(cc>=0 && cc< int(nb_characters)) 
    input_line.push_back(Utf8Char(cc));
}

// Like get_a_newline below. But we never print the line in the log file.
// Reading can be from the terminal or an external file.
// Return value is false in case of EOF.
// Knuth says: an empty line is appended at the end of a read_file
// This is done if spec is true
bool Parser::new_line_for_read(bool spec)
{
  state = state_N;
  static char m_ligne [4096];
  static int tty_line_no = 0;
  int n = 0;
  scratch.reset();
  input_line.clear();
  if(cur_in_chan==nb_input_channels) {
    readline(m_ligne,78);
    tty_line_no++;
    n = tty_line_no;
    scratch.push_back(m_ligne);
  } else
    n = tex_input_files[cur_in_chan].get_lines().get_next(scratch);
  if(n<0) {
    tex_input_files[cur_in_chan].close();
    if(!spec) return false;
  } else {
    store_new_line(n,false);
  }
  insert_endline_char();
  return true;
}

// Read a new line from the source. If force_eof is false, a tentative
// is made to read from the current file. If no file is found, reading is 
// aborted if either no_new_file is true or there is no other file.
// If a line has been read, force_eof is set to false.
// In the case of \input\foo\input bar, after foo is finished, the 
// pop_input_stack may give us a non-empty line, even a non-empty TL
// And consider the case \def\foo{\input A\input B\relax}
bool Parser::get_a_new_line ()
{
  state = state_N;
  input_line.clear();
  scratch.reset();
  int n = -2;
  if(cur_in_chan<=nb_input_channels) 
    return true; // this is the wrong function to call
  if(force_eof) {
    lines.clear();
    force_eof = false;
  } else {
    n = lines.get_next(scratch);
    if(n<0 && !seen_eof) {
      seen_eof = true;
      TokenList L = toks_registers[everyhbox_code].get_val();
      if(!L.empty()) { back_input(L); return false; }
    }
  }
  if(n<0) {
    main_ns::file_ended = true;
    // force_eof is true, or cur file ended
    bool sz =cur_input_stack.size()==0;
    cur_tok = hash_table.relax_token;
    if(sz || cur_input_stack.back()->get_name() !="(reevaluate)" ) 
      check_outer_validity0();
    if(sz && tracing_io())
      the_log << lg_start << "++ Input stack empty at end of file\n";
    if(cur_in_chan==token_in_chan || sz) return true;
    pop_input_stack(true);
    return false;
  }
  store_new_line(n, tracing_io());
  insert_endline_char();
  return false;
}

// This implements \read.
// Reads from file (or the tty if the file is closed).
// A whole line is read. If braces are unbalanced, a second (or third...)
// line is read. 
TokenList Parser::read_from_file(int ch, bool rl_sw)
{
  string file_name = "tty";
  if(ch<0 || ch>=nb_input_channels) 
    cur_in_chan = tty_in_chan;
  else if (!tex_input_files[ch].is_valid()) 
    cur_in_chan = tty_in_chan; 
  else { 
    cur_in_chan = ch;
    file_name = tex_input_files[ch].get_lines().get_file_name();
  }
  push_input_stack(file_name,false);
  TokenList L;
  new_line_for_read(true);
  int b = 0;
  if(rl_sw) { // case of readline, only one line is read
    for(;;) {
      if(at_eol()) break;
      Utf8Char c = input_line[input_line_pos++];
      if(c.get_value()==' ')
	L.push_back(hash_table.space_token);
      else L.push_back(Token(other_t_offset,c));
    }
  } else {
    for(;;) {
      if(at_eol()) { // can we stop here ?
	if(b==0) break;  // Ok, brace level is zero
	if(!new_line_for_read(false)) { // a file cannot end here
	  parse_error("unbalanced \\read");
	  while(b>0) { L.push_back(hash_table.CB_token); b--; }
	  break; // stop because file ended
	}
      }
      if(next_from_line())
	continue; // we might be at EOL here
      if(cur_tok.is_CB_token()) {
	if(b==0) {
	  parse_error("too many closing braces in \\read");
	  continue;
	}
	else b--;
      }
      L.push_back(cur_tok);
      if(cur_tok.is_OB_token()) b++;
    }
  }
  // common ending
  if(ch<16) tex_input_files[ch].get_line_no() = get_cur_line();
  pop_input_stack(false);
  cur_in_chan = main_in_chan;
  return L;
}

// Fills the token list with the content of the buffer.
// main job is done by read_into.
void Parser::add_buffer_to_list(Buffer& b, TokenList& X,const string& name)
{
  SaveScannerStatus tmp(ss_normal);
  push_input_stack(name,false);
  uint n  = cur_input_stack.size();
  bool s = restricted;
  restricted = false;
  b.push_back('\n');
  lines.push_front(Clines(-1));
  lines.push_front(Clines(1,b.to_string(),true));
  read_into(X);
  restricted = s;
  if(n == cur_input_stack.size())
    pop_input_stack(false);
}

// read until EOF in the list L. Ignores the final \n.
void Parser::read_into(TokenList& L)
{
  main_ns::file_ended = false;
  for(;;) {
    bool res = next_from_line0();
    if(!res) { L.push_back(cur_tok); continue; }
    if(main_ns::file_ended) break;
    if(!at_eol()) continue;
    if(get_a_new_line()) break;
    if(main_ns::file_ended) break;
  }
  if(L.empty()) return;
  if(L.back()==hash_table.newline_token) L.pop_back();
}

// section 3. scan_int and related functions


// Error signaled for bad mu (there is something in local_buf)
void scan_ns::mu_error()
{
  err_ns::local_buf << bf_reset << "Incompatible glue units in " 
		    << the_parser.get_name_for_error().tok_to_str() <<"\n" 
		    << local_buf; 
  err_ns::signal_error("Incompatible glue units",0);
}

// Error signaled when a token list in seen a number expected
void Parser::bad_number()
{
  err_ns::local_buf <<bf_reset << "Missing number, treated as zero\n";
  err_ns::local_buf << "found token list " << cur_tok.tok_to_str();
  err_ns::local_buf << " while scanning " << name_for_error.tok_to_str();
  err_ns::signal_error("Missing number",0);
  cur_val.set_dim(0);
}

// This function calls scan_int for the token t,
// and checks that the result is between 0 and n (inclusive).
int Parser::scan_int(Token t, int n, String s)
{
  int N = scan_int(t);
  if(N<0 || N>n) {
    err_ns::local_buf <<bf_reset << "Bad " << s << " replaced by 0\n";
    err_ns::local_buf << t.tok_to_str() << " wants 0<=N<=" << n 
		      << ", with N=" <<N;
    err_ns::signal_error("number too big",0);
    cur_val.set_int_val(0); 
    return 0;
  }
  return N;
}

// The following functions all call the previous scan_int.
// 
int Parser::scan_eight_bit_int  ()
{
  return scan_int(cur_tok,nb_registers-1,"register code");
}

int Parser::scan_27bit_int()
{
  return scan_int(cur_tok,(1<<27)-1,"character code");
}

int Parser::scan_char_num()
{
  return scan_int(cur_tok,scan_char_num_max,"character code");
}

int Parser::scan_fifteen_bit_int()
{
  return scan_int(cur_tok,32767,"mathchar");
}

int Parser::scan_twenty_seven_bit_int()
{
  return scan_int(cur_tok,(1<<27)-1,"delimiter code");
}

// \setcounter{foo}{10} is the same as \c@foo=10\relax. We use a space
// instead of relax. This function reads a token list, puts it back in the
// input, then calls scan_int.
// Code changed : we use the next command
int Parser::scan_braced_int(Token T)
{
  TokenList L = mac_arg();
  return scan_int(L,T);
}

int Parser::scan_int(TokenList&L,Token T)
{
  Token marker = hash_table.relax_token;
  back_input(marker);
  back_input(L);
  int n = scan_int(T);
  read_until(marker);
  return n;
}


// Here we may have an optional argument; default value is d
int Parser::scan_special_int_d(Token T, int d)
{
  TokenList L; next_optarg(L);
  if(L.empty()) {
    if(tracing_commands()) 
      the_log << lg_startcond << "scanint for "<< T <<"->" << d <<lg_end;
    return d;
  }
  back_input(hash_table.space_token);
  back_input(L);
  return scan_int(T);
}

// This is the one argument scanint function. It calls two auxiliary functions
// puts the result in cur_val, and returns it. It may log it.
int Parser::scan_int(Token T) 
{
  bool negative = scan_sign();
  int val = scan_int_internal();
  if(negative)  val = -val; 
  cur_val.set_int_val(val);
  cur_val.set_type(it_int);
  if(tracing_commands()) 
    the_log << lg_startcond << "scanint for "<< T <<"->" << cur_val <<lg_end;
  return val;
}

// Scan a sign (plus, minus, spaces, etc.). After that cur_tok is the first 
// unread token.
bool Parser::scan_sign()
{
  bool negative = false;
  for(;;) {
    get_x_token();
    if(cur_cmd_chr.is_space()) continue;
    if(cur_tok.is_minus_token())
      negative = ! negative;
    else if(!cur_tok.is_plus_token())
      return negative;
  }
}

// This is now the scan_int routine. It reads either `\A or
// \language or '12345.
int Parser::scan_int_internal()
{
  if(cur_tok.is_invalid()) {
    parse_error("Missing number, treated as zero");
    return 0;
  }
  if(cur_tok.is_backquote())
    return scan_alpha();
  else if(cur_cmd_chr.is_ok_for_the()) {
    scan_something_internal(it_int,false);
    return cur_val.get_int_val();
  } else return scan_int_digs();
}

// Read an alpha token. 
// we allow }, \} or active ~/ One optional space is read.
int Parser::scan_alpha()
{
  if(get_token()) {
    improper_alpha();
    return 0;
  }
  if(cur_tok.get_val() <= int(last_ok_for_char_cst)) {
    int t = cur_tok.chr_val();
    read_one_space();
    return t;
  } else {
    improper_alpha();
    return 0;
  }
}


// This reads the digits for scan_int, with an overflow check.
// I do not like the setting of the global variable radix.
// This variable is saved/restored by  get_x_token(), so a local variable
// could be used. How
int Parser::scan_int_digs()
{
  int radix = 10;
  int m = 214748364;
  int ok_so_far = 0;
  if(cur_tok.is_singlequote()) { 
    radix = 8; m = 1<<28; get_x_token();
  } else if(cur_tok.is_doublequote()) { 
    radix = 16; m = 1<<27; get_x_token();
  }
  bool vacuous = true;
  int val = 0;
  for(;;) {
    int d = cur_tok.tex_is_digit(radix);
    if(d==-1) break;
    vacuous = false;
    if(val>=m &&(val>m || d>7 || radix != 10)) {
      if(!ok_so_far) {
	parse_error("number too big");
      } else if(ok_so_far>1000) {
	parse_error("number too big (infinite recursion?)");
	get_token();
	return 0;
      }
      ok_so_far++;
      val = max_integer; // this is 2^{31}-1
    } else val = val*radix +d;
    get_x_token();
  }
  if(vacuous) { // cur_tok cannot be a space here
    back_input();
    parse_error("Missing number, treated as zero scanned by ",name_for_error,"",
		"missing number");
  } else back_input_unless_space();
  return val;
}

// This is the interface of the scan_something_internal routine.
// We first call scan_something_internal(?,?), 
// then change the level, and (maybe) change the sign of the result.
void Parser::scan_something_internal(internal_type level, bool negative)
{
  Token t = cur_tok;
  scan_something_internal(level);
  cur_val.change_level(level);
  if(negative) cur_val.negate();
}

// A mu is a math-unit. it is one quad divided by 18. 
// but the quad depends on the current size. This explains some errors
// e.g. $x\mskip 18mu y^{x\mskip18mu y}$ : different spaces
// in plain tex, 10pt, we have 9.99976 and 8.19443
// void io_ns::mu_error(){}


String io_ns::dump_it(internal_type s)
{
  switch(s) {
  case it_int: return "integer";
  case it_dimen: return "dimension";
  case it_glue: return "glue";
  case it_mu: return "muglue";
  case it_ident: return "font indentifier";
  case it_tok: return "token";
  default: return "impossible";
  }
}

// Remember: type and level in decreasing order:
//   it_mu, it_glue, it_dimen,it_int
// (there are other types, but not here). Here type can only decrease.
void SthInternal::change_level(internal_type level)
{
  while(type>level) {
    if(type==it_glue)
      set_int_val(get_glue_width());
    else if(type==it_mu) {
      local_buf << bf_reset 
		<< "in conversion from mu to " << io_ns::dump_it(level); 
      scan_ns::mu_error();
    }
    type = internal_type(type -1);
  }
}

// This is the scan_something_internal routine proper.
// The variable level is used only in case the caller cannot change the level
// For instance, if you say \count0 = \everypar.
// Variables like \lastskip have no equivalent in Tralics, because the lastskip
// may depend on the value of \spacefactor, and quantities defined only 
// in fonts. 
void Parser::scan_something_internal(internal_type level)
{
  subtypes m = cur_cmd_chr.get_chr();
  int v;
  switch(cur_cmd_chr.get_cmd()) {
  case char_given_cmd:     // result of \chardef
  case math_given_cmd:
    cur_val.set_int(m);
    return;
  case last_item_cmd:  // \lastkern etc
    switch(m) {
    case inputlineno_code: 
      cur_val.set_int(get_cur_line()); return;
    case lastpenalty_code:
    case badness_code:
    case lastnodetype_code:
      cur_val.set_int(0);
      return;
    case xmllastid_code:
      cur_val.set_int(the_stack.get_xid().value); return;
    case xmlcurrentid_code: 
      cur_val.set_int(the_stack.cur_xid().value); return;
    case currentgrouplevel_code:
      cur_val.set_int(cur_level-1); return;
    case currentgrouptype_code:
      cur_val.set_int(cur_group_type()); return;
    case etexversion_code: 
      cur_val.set_int(2); return;
    case currentiflevel_code: 
      cur_val.set_int(conditions.top_level()); return;
    case currentiftype_code:
      cur_val.set_int(conditions.top_type()); return;
    case currentifbranch_code:
      cur_val.set_int(conditions.top_branch()); return;
    case fontcharwd_code: 
    case fontcharht_code:
    case fontchardp_code:
    case fontcharic_code:
      scan_font_ident();
      scan_char_num();
      cur_val.set_dim(0); 
      return;
    case parshapelength_code:
    case parshapeindent_code:
    case parshapedimen_code:
      par_shape(m);
      return;
    case mutoglue_code:
      scan_glue(it_mu,cur_tok);
      cur_val.set_type(it_glue);
      return;
    case gluetomu_code:
      scan_glue(it_glue,cur_tok);
      cur_val.set_type(it_mu);
      return;
    case gluestretchorder_code:
    case glueshrinkorder_code:
    case gluestretch_code:
    case glueshrink_code:
      scan_glue(it_glue,cur_tok);
      cur_val.get_info(m);
      return;
    case numexpr_code:
    case dimexpr_code:
    case glueexpr_code:
    case muexpr_code:
      scan_expr(m);
      return;
    default: // lastkern lastskip
      cur_val.set_dim(0);
      return;
    }
  case toks_register_cmd: // \toks 349
  case assign_toks_cmd:   // \output, \everypar etc
    if(level != it_tok) { // perhaps we wanted a dimen here...
      bad_number();
      return;
    }
    v = m;
    if(cur_cmd_chr.get_cmd()== toks_register_cmd) //read the 349 in `\toks349'
      v = scan_eight_bit_int();
    cur_val.set_toks(toks_registers[v].get_val());
    return;
  case assign_int_cmd: // \year etc
    cur_val.set_int(eqtb_int_table[m].get_val());
    return;
  case assign_dimen_cmd: // \parindent etc
    cur_val.set_dim(eqtb_dim_table[m].get_val());
    return;
  case assign_glue_cmd:  // \baselineskip etc
    cur_val.set_glue(glue_table[m].get_val());
    return;
  case assign_mu_glue_cmd: // \thinmuskip etc
    cur_val.set_mu(glue_table[m].get_val());
    return;
  case assign_font_dimen_cmd:  // \fontdimen
    {
      int a = scan_int(cur_tok);         // read the position in the table
      int k = scan_font_ident();         // read the font ID
      cur_val.set_dim(tfonts.get_dimen_param(k,a).get_value());
      return;
    }
  case assign_font_int_cmd: // hyphenchar, \skewchar. Read a font ID
    {
      int k = scan_font_ident();
      cur_val.set_int(tfonts.get_int_param(k,m));
      return;
    }
  case assign_enc_char_cmd: 
    {
      Token T = cur_tok;
      int enc = scan_int(T);
      int pos = scan_int(T);
      cur_val.set_int(io_ns::get_enc_param(enc,pos));
      return;
    }
  case set_mathprop_cmd: 
    {
      int k = scan_mathfont_ident();
      int w = eqtb_int_table[mathprop_ctr_code].get_val();
      w = (w & (1<<k)) ? 1 : 0;
      cur_val.set_int(w);
      return;
    }
  case set_mathchar_cmd: // returns the cur math char as a token list
    {
      Token T = cur_tok;
      int k = scan_mathfont_ident();
      int v = scan_int(T,127,"mathchar");
      if(level != it_tok) { 
	cur_tok = T;bad_number();
	return;
      }
      string s = get_math_char(uchar(v),k);
      cur_val.set_toks(token_ns::string_to_list(s,false)); 
      return;
    }
  case set_aux_cmd:        // \spacefactor etc
    if(m==0) cur_val.set_int(0); // \spacefactor
    if(m==1)  cur_val.set_dim(0);
    else if(m==2) cur_val.set_int(lines.get_encoding()); 
    else if(m==3) cur_val.set_int(the_main->get_input_encoding()); 
    else if(m==4)  cur_val.set_int(cur_font.get_size()); 
    return;
  case set_prev_graf_cmd:  // \prevgraf
    cur_val.set_int(0);
    return;
  case set_page_dimen_cmd: // \pagedepth etc
    cur_val.set_dim(0);
    return; 
  case set_page_int_cmd:   // \deadcycles etc
    cur_val.set_int(0);
    return;
  case set_shape_cmd:      // \parshape
    if(m != parshape_code) {
      int k = scan_int(cur_tok);
      if(k<0) cur_val.set_int(0);
      else if(k==0) cur_val.set_int(penalties[m-1].size());
      else if(k <= int(penalties[m-1].size()))	
	cur_val.set_int(penalties[m-1][k-1]);
      else cur_val.set_int(penalties[m-1].back());
    } else cur_val.set_int(parshape_vector.size()/2);
    return;
  case set_box_dimen_cmd:  // \width, \height \depth
    scan_eight_bit_int();
    cur_val.set_dim(0);
    return;
  case def_code_cmd: // \catcode, \lccode etc
    v = scan_char_num();
    cur_val.set_int_val (eqtb_int_table[v+m].get_val());
    cur_val.set_type(it_int);
    return;
  case def_family_cmd:    // \textfont
  case set_font_cmd:      // a font (like \tenrm in plain)
  case def_font_cmd:      // \font
    if(level != it_tok) { // perhaps we wanted a dimen here...
      bad_number();
      return;
    }
    { 
      back_input();  // push back, and use scan_font_ident as parser.
      int k=scan_font_ident(); 
      const string& s = tfonts.name(k);
      cur_val.set_toks(token_ns::string_to_list(s,false)); 
    }
    return;
  case register_cmd: // \count, \dimen, etc
    v = scan_eight_bit_int();
    switch(m) {
    case it_int: 
      cur_val.set_int(eqtb_int_table[v+count_reg_offset].get_val()); return;
    case it_dimen: 
      cur_val.set_dim(eqtb_dim_table[v+dimen_reg_offset].get_val()); return;
    case it_glue: 
      cur_val.set_glue(glue_table[v].get_val()); return;
    case it_mu: 
      cur_val.set_mu(glue_table[v+muskip_reg_offset].get_val()); 
      return;
    default:
      parse_error("Confusion in \\register");
      return;
    }
  default:
    parse_error("You can't use `",cur_tok, "' after \\the", 
		"You can't use x after...");

  }
}

// Aux function for \parshapeXXX, XXX= length indent or dimen
void Parser::par_shape(subtypes m)
{
  int v = scan_int(cur_tok);
  cur_val.set_dim(0);
  if(v<=0) return;
  int n = parshape_vector.size()/2;
  if(n==0) return;
  int q =0;
  if(m==parshapelength_code) q = 0;
  else if(m==parshapeindent_code) q = 1;
  else {
    if(v&1) q=1;
    v = (v+q)/2;
  }
  if(v>=n) v= n;
  v = 2*v-q-1;
  cur_val.set_dim(parshape_vector[v]);
} 




// This implements \the. The result is a token list, of catcode 12
// (except for space). The idea is to call scan_something_internal.
// If this does not give a token list, we convert it to a string,
// then use the str_toks command. 
// Newline is converted to space. Is this a good idea? 
TokenList Parser::the_toks ()
{
  static Buffer B;
  subtypes c = cur_cmd_chr.get_chr();
  if(c==unexpanded_code) {
   TokenList L = scan_general_text();
   return L;
  }
  if(c==detokenize_code) {
    TokenList L = scan_general_text();
    B.reset();
    B << L; 
    return B.str_toks(true);
  }
  if(tracing_commands()) the_log.dump("the");
  get_x_token(); 
  if(tracing_commands()) 
    the_log << lg_startbracebs << "the " << cur_tok << lg_endbrace;
  scan_something_internal(it_tok,false);
  B.reset();
  switch(cur_val.get_type()) {
  case it_ident:  // case of a font name
  case it_tok:    // case of a token list
    return cur_val.get_token_val();
  case it_int:
    B.push_back_int(cur_val.get_int_val());
    break;
  case it_dimen: 
    B.push_back(ScaledInt(cur_val.get_int_val()),glue_spec_pt); 
    break;
  case it_glue: 
    B.push_back(cur_val.get_glue_val()); 
    break;
  case it_mu: 
    B.push_back(cur_val.get_glue_val()); 
    B.pt_to_mu();
    break;
  }
  return B.str_toks(true);
}

// Next objective is scan_dimen. This can read a real number (in fact
// scaled integers)

// Assume that we have read x=0.142. i.e. k=3 digits  in the table.
// This computes y=x* 2^{17}, then (y+1)/2.
void RealNumber::convert_decimal_part(int k, int*table)
{
  int f = 0;
  while(k>0) {
    k--;
    f = (f+ (table[k] <<17))/10;
  }
  fpart =  (f+1)/2;
}

// Scans something like 2.5, may skip a final space.
// In the case of "17.2, scan_int set radix to non-ten, so that the result
// is an integer. Same for `\A, and '177.
void Parser::scan_double(Token T,RealNumber& res)
{
  bool is_decimal  = true; 
  // Case 0.5 is the same as .5, case of initial dot is trivial
  if(!cur_tok.is_dec_separator()) {
    get_x_token();
    if(cur_tok.is_invalid()) {
      parse_error("Missing number, treated as zero");
      return;
    }
    int val = 0;
    if(cur_tok.is_backquote()) {
      is_decimal = false;
      val = scan_alpha();
    } else if(cur_cmd_chr.is_ok_for_the()) {
      is_decimal = false;
      scan_something_internal(it_int,false);
      val = cur_val.get_int_val();
    } else { 
      if(cur_tok.is_singlequote() || cur_tok.is_doublequote())
	is_decimal = false;
      val = scan_int_digs();
    }
    if(tracing_commands()) 
      the_log << lg_startcond << "scanint for "<< T <<"->" << val <<lg_end;
    res.set_ipart(val); // this reads the integer part
  }
  if(!(is_decimal && cur_tok.is_dec_separator())) return;
  get_token(); // read the . or ,
  int table[17];
  int k = 0;
  for(;;) {
    get_x_token(); 
    if(!cur_tok.is_digit_token())
      break;
    if(k<17) {
      table[k] = cur_tok.val_as_digit();
      k++;
    }
  }
  back_input_unless_space();
  res.convert_decimal_part(k,table);
}

// This is a bit more efficient then a lot of scan_keyword...
// it reads a unit, returns 
int Parser::read_unit()
{
  remove_initial_space();
  Utf8Char c1=0, c2=0;
  if(cur_tok.is_a_char()) {
    c1 = cur_cmd_chr.char_val().to_lower();
    Token T = cur_tok;
    get_x_token();
    if(cur_tok.is_a_char()) {
      c2 = cur_cmd_chr.char_val().to_lower();
      if(c1=='p' && c2=='t') return unit_pt;
      if(c1=='i' && c2=='n') return unit_in;
      if(c1=='p' && c2=='c') return unit_pc;
      if(c1=='c' && c2=='m') return unit_cm;
      if(c1=='m' && c2=='m') return unit_mm;
      if(c1=='b' && c2=='p') return unit_bp;
      if(c1=='d' && c2=='d') return unit_dd;
      if(c1=='c' && c2=='c') return unit_cc;
      if(c1=='s' && c2=='p') return unit_sp;
    }
    if(!cur_tok.is_invalid()) back_input();
    cur_tok = T;
  }
  if(!cur_tok.is_invalid()) back_input();
  parse_error("Missing unit (replaced by pt) ", cur_tok,"","missing unit");
  return unit_pt;
}

// Assume that we have read the -3.5 in -3.5cm
// We have to read the cm and convert the cm into sp.
void Parser::scan_unit(RealNumber R)
{
  scan_keyword("true"); // \mag is ignored, hence no multiply....
  int k = read_unit();
  if(k==unit_sp) {
    cur_val.set_int_val(R.get_ipart());
    cur_val.attach_sign(R.get_negative());
    return; // special ignore frac. part
  }
  static int numerator_table[] =   {0,7227,12,7227,7227,7227,1238,14856,0};
  static int denominator_table[] = {0, 100, 1, 254,2540,7200,1157, 1157,-1};
  int num = numerator_table[k];
  int den = denominator_table[k];
  if(k != unit_pt) { 
    int i = R.get_ipart(); int f = R.get_fpart();
    int remainder;
    i = arith_ns::xn_over_d(i,num,den,remainder);
    f = (num*f + (remainder <<16))/den;
    i += f>>16;
    R.set_fpart(f% (1<<16));
    R.set_ipart(i);
  }
  cur_val.attach_fraction(R);
}

// If mu is true we want math_units. This procedure returns false
// in case no internal quantity is there. If there is an internal quantity
// we accept either an integer (a potential factor) or a dimen, skip or muskip
bool Parser::scan_dim_helper(bool mu,bool allow_int)
{
  if(!cur_cmd_chr.is_ok_for_the()) {
    if(!cur_tok.is_invalid()) back_input();
    return false;
  }
  if(mu) {
    scan_something_internal(it_mu,false); 
    cur_val.glue_to_mu(); // Ignores stretch and shrink if desired
    if(cur_val.is_mu()) return true;
    if(allow_int && cur_val.is_int()) return true;
    local_buf << bf_reset << "Converting to mu from " 
	      << io_ns::dump_it(cur_val.get_type());
    scan_ns::mu_error();
  }
  else scan_something_internal(it_dimen,false);
  return true;
}

// This is the start of scan_dimen. It is not called if scan_glue reads
// and integer and calls scan_dimen for more.
// In the case of a number like 1.5, we just call scan_double. and return false
// Otherwise, if we see \parindent or -\parindent, we put the value in
// cur_val, and return true. If we see \count0 (in integer), we put it in R
// and return false

bool Parser::scan_dim2(RealNumber&R, bool mu,Token T)
{
  R.set_negative(scan_sign());
  if(!scan_dim_helper(mu,true)) {
    scan_double(T,R);
    return false;
  }
  bool res = false;
  if(mu) {
    if(cur_val.is_mu())
      res = true;
  } else {
    if(cur_val.is_dimen())
      res = true;
    // We could have \dimen0=\count0 \dimen1 ....
  }
  if(res)
    cur_val.attach_sign(R.get_negative());
  else 
    R.from_int(cur_val.get_int_val());
  return res;
}

// Case 1: shortcut is false. If scan_dim2 return true,  we are done.
// Otherwise we have a value (integer or real number in value).
// Case 2: shortcut is true: we have a number in cur_val, and copy it in value.
// Hence, assume value holds a real number.
// Case 3. inf is true. We accept 1.2fill or \count0 filll
// Case 4: not inf, or no fill seen. We accept 2.5\parindent
// or \count0\skip15 (but not 1.2 3.4). Via scan_dime_helper
// Case 5: Otherwise, we need a unit, either cm, or mu or ex. 
// Returns true if the last token read was a keyword

bool Parser::scan_dimen1(bool mu, bool inf, glue_spec& co, bool shortcut, Token T)
{
  RealNumber value;
  if(!shortcut) {
    if(scan_dim2(value,mu,T))
      return false;
  } else 
    value.from_int(cur_val.get_int_val());
  if(inf) {
    if(scan_keyword("fil")) {
      co = glue_spec_fil;
      while(scan_keyword("l")) {
	if(co==glue_spec_filll) {
	  parse_error("Illegal unit of measure (replaced by filll)");
	} else co = glue_spec(co+1);
      }
      cur_val.attach_fraction(value);
      return true;
    }
  }
  remove_initial_space();
  if(scan_dim_helper(mu,false)) {
    multiply_dim(value,cur_val.get_int_val());
    return false;
  }

  if(mu) {
    if(scan_keyword("mu")) {
      cur_val.attach_fraction(value);
      return true;
    }
    local_buf <<bf_reset << "Expected mu unit, found something else";
    scan_ns::mu_error();
  }
  else {
    if(scan_keyword("em")) {
      multiply_dim(value,10<<16);
      return true;
    }
    if(scan_keyword("ex")) {
      multiply_dim(value,17<<14); // 4.25 vs 0.430555 in cmr10
      return true;
    }
  } 
  scan_unit(value);
  return true;
}

// If scan_dimen1 return true, we have to read an optional space.
// In any case, we print something in the log file.
void Parser::scan_dimen(bool mu,Token T)
{
  glue_spec unused;
  scan_dimen(mu,false,unused,false, T);
}

void Parser::scan_dimen(bool mu, bool inf, glue_spec&co,bool shortcut, Token T)
{
  bool skip = scan_dimen1(mu,inf,co,shortcut,T);
  if(skip) read_one_space();
  cur_val.set_type(it_dimen);
  if(tracing_commands()) 
    the_log << lg_start 
	    << local_buf.trace_scan_dimen(T,cur_val.get_dim_val(),mu);
}

// Multiplies a dimension by an integer
void Parser::multiply_dim(RealNumber val,int v)
{
  int rem; // unused but modified
  int A = arith_ns::xn_over_d(v,val.get_fpart(),1<<16,rem);
  int B = arith_ns::nx_plus_y(val.get_ipart(),v,A);
  cur_val.set_int_val(B);
  cur_val.attach_sign(val.get_negative());
}

// put x in this, but change the sign is *this<0
void RealNumber::from_int(int x)
{
  if(negative) x = -x;
  if(x>0) { ipart = x; fpart = 0; negative = false; }
  else { ipart = -x; negative = true; fpart = 0 ; }
}

// This reads glue.
// level is either it_glue or it_mu. The call to scan_something_internal
// signals an error if current value is a token list, and sets the type
// to something else.
// In any case
 
void Parser::scan_glue(internal_type level,Token T)
{
  bool negative = scan_sign();
  bool mu = level==it_mu;
  glue_spec co = glue_spec_pt;
  if(cur_cmd_chr.is_ok_for_the()) { // got something to evaluate
    scan_something_internal(level,negative);
    internal_type ct = cur_val.get_type();
    switch (ct) {
    case it_int :
      scan_dimen(mu,false,co,true,T); // we have a multiplier 
      break;
    case it_dimen:
      if(!mu) break; 
      local_buf << bf_reset
		<< "in conversion from dimen to " << io_ns::dump_it(level); 
      scan_ns::mu_error();
      break;
    case it_glue: // we have the value
    case it_mu:
      // Note that ct<=level, different means from ct=glue to level=mu
      if(level!=ct) { // Assume level = glue, ct = mu 
	local_buf << bf_reset << "in conversion from glue to mu";
	scan_ns::mu_error();
	cur_val.set_type(level);
      }
	return;
    default:
      // This should not happen 
      parse_error("Unexpected error in ", name_for_error,"","bad");
      cur_val.set_int_val(0); 
      break;
    }
  } else { 
    if(!cur_tok.is_invalid()) back_input();
    scan_dimen(mu,false,co,false,T);
    if(negative) cur_val.fast_negate();
  }
  Glue q;

  q.set_width(cur_val.get_int_val());
  if(scan_keyword("plus")) {
    co = glue_spec_pt;
    scan_dimen(mu,true,co,false,T);
    q.set_stretch(cur_val.get_int_val()); 
    q.set_stretch_order(co);
  }
  if (scan_keyword("minus")) {
    co = glue_spec_pt;
    scan_dimen(mu,true,co,false,T);
    q.set_shrink(cur_val.get_int_val()); 
    q.set_shrink_order(co);
  }
  cur_val.set_glue_val(q);
  cur_val.set_type(level);
  if(tracing_commands()) 
    the_log << lg_startbrace << "scanglue " << cur_val << lg_endbrace;
}


// interface to scan_glue. If opt is true reads an optional argument
// If no optional argument is given, scan_glue_opt is set to false,
// and nothing is done. Otherwise, an argument is read (braces or
// parens), and pushed back.
void Parser::scan_glue(internal_type level,Token t,bool opt)
{
  TokenList T;
  if(opt) {
    next_optarg(T);
    scan_glue_opt = true;
    if(T.empty()) scan_glue_opt = false;
    if(!scan_glue_opt) return;
  }
  else 
    T = mac_arg();
  back_input(hash_table.relax_token);
  back_input(T);
  scan_glue(level,t);
  get_token();
  if(cur_tok!=hash_table.relax_token)
    back_input();
}

// Calls scan_glue; returns 0 if no argument given.
Istring Parser::get_opt_dim(Token t)
{
  scan_glue(it_glue,t,true);
  if(!scan_glue_opt) return Istring();
  return the_main->SH.find_scaled(ScaledInt(cur_val.get_glue_width()));
}

void Parser::scan_glue(internal_type level,Token t, String B)
{
  TokenList L = token_ns::string_to_list(B,false);
  back_input(hash_table.relax_token);
  back_input(L);
  scan_glue(level,t);
  get_token();
  if(cur_tok!=hash_table.relax_token)
    back_input();
}

// Handles a command like \def, that allows \global before it.
void Parser::prefixed_aux(bool gbl, symcodes K)
{
  int chr = cur_cmd_chr.get_chr(); 
  Token T = cur_tok; // for debug...
  name_for_error = T;
  int p; 
  int q = 0;
  switch(cur_cmd_chr.get_cmd()) {
  case set_font_cmd:  
    word_define(cur_font_loc,chr,gbl);
    return;
  case def_cmd:
  case let_cmd:
    define_something(chr,gbl,K);
    return;
  case shorthand_def_cmd:
    shorthand_define(chr,gbl);
    return;
  case read_to_cs_cmd: 
    {
      int ch = scan_int(T); 
      if(!scan_keyword("to"))
	parse_error("Missing to inserted");
      get_r_token();
      Token pp = cur_tok;
      scanner_status = ss_defining;
      TokenList L = read_from_file(ch,chr==one_code);
      new_macro(L,pp,gbl);
      scanner_status = ss_normal;
      return;
    }
  case toks_register_cmd:            // ok ???
  case assign_toks_cmd: 
    read_a_TokenList(gbl);
    return;
  case assign_int_cmd: 
    p = chr; 
    scan_optional_equals(); 
    q = scan_int(T);
    word_define(p,q,gbl);
    return;
  case assign_dimen_cmd: 
    p = chr; 
    scan_optional_equals();
    scan_dimen(false,T); 
    dim_define(p,cur_val.get_dim_val(),gbl);
    return;
  case assign_glue_cmd:
  case assign_mu_glue_cmd: 
    {
      p = chr; 
      int n = cur_cmd_chr.get_cmd(); 
      scan_optional_equals();
      if (n==assign_mu_glue_cmd) scan_glue(it_mu,T); 
      else scan_glue(it_glue,T);
      glue_define(p,cur_val.get_glue_val(),gbl);
      return;
    }
  case def_code_cmd:
    assign_def_something(gbl);
    return;
  case def_family_cmd: 
    {
      int a= scan_int(cur_tok,15,"family number");
      scan_optional_equals(); 
      int c = scan_font_ident();
      word_define(a+chr,c,gbl);
      return;
    }
  case set_mathprop_cmd:
    {
      int k = scan_mathfont_ident();
      int mask = 1 << k; 
      uint w = eqtb_int_table[mathprop_ctr_code].get_val();
      scan_optional_equals();
      int v = scan_int(T);
      if(v) w |= mask;
      else w &= ~mask;
      word_define (mathprop_ctr_code,w,gbl);
      return;
    }
  case set_mathchar_cmd: // set a slot in the math font table
    {
      int k = scan_mathfont_ident();
      int v = scan_int(T,127,"mathchar");
      scan_optional_equals();
      flush_buffer();
      string value = sT_next_arg();
      set_math_char(v,k,value);
      return;
    }
  case register_cmd:
  case advance_cmd:
  case multiply_cmd:
  case divide_cmd: 
    do_register_command(gbl);
    return;
  case set_box_cmd: { // \setbox0 = 
    int i = scan_eight_bit_int();
    scan_optional_equals();
    scan_box(gbl? setbox_offset+i : i);
    return;
  }
  case set_aux_cmd:
    scan_optional_equals();
    if(chr==1) scan_dimen(false,T); // \prevdepth
    else { 
      q = scan_int(T); // \spacefactor or \input@encoding
      if(chr==2) lines.change_encoding(q);
      if(chr==3) the_main->set_input_encoding(q);
      if(chr==4) {
	cur_font.change_size(q);
	font_has_changed();
      }
    } 
    return;
  case set_prev_graf_cmd:
    scan_optional_equals(); 
    scan_int(T); 
    return; // ignore value
  case set_page_dimen_cmd:
    scan_optional_equals(); 
    scan_dimen(false,T); 
    return; // ignore value
  case set_page_int_cmd: 
    scan_optional_equals(); 
    scan_int(T);
    return; // ignore value
  case set_box_dimen_cmd:
    scan_eight_bit_int(); 
    scan_optional_equals();
    scan_dimen(false,T);
    return; // ignore value
  case set_shape_cmd:
    {
      scan_optional_equals(); 
      q = scan_int(T); 
      if(chr==parshape_code) {
	if(q<=0) { parshape_vector.resize(0); return; }
	q = 2*q;
	parshape_vector.resize(q);
	for (int j=0;j<q;j++) {
	  scan_dimen(false,T); 
	  parshape_vector[j] = cur_val.get_dim_val();
	} 
      } else {
	vector<int>& V = penalties[chr-1];
	if(q<0) { V.resize(0); return; }
	V.resize(q);  
	for (int j=0;j<q;j++) V[j] = scan_int(T);
      }
    }
   return;
  case assign_font_dimen_cmd: 
    {
      int a= scan_int(T); 
      int b = scan_font_ident();
      scan_optional_equals(); 
      scan_dimen(false,T);
      int c = cur_val.get_int_val();
      tfonts.set_dimen_param(b,a,c);
      return;
    }
  case assign_font_int_cmd: 
    {
      int k = scan_font_ident();
      scan_optional_equals();
      int v = scan_int(T);
      tfonts.set_int_param(k,chr,v);
    return;
    }
  case assign_enc_char_cmd: 
    {
      int enc = scan_int(T);
      int pos = scan_int(T);
      scan_optional_equals();
      int v = scan_int(T);
      if(pos<0 && v>=0 && pos-v<= 256) {
	int start = v; 
	int bound = v-pos;
	for(int i=start; i<bound; i++) {
	  v = scan_int(T);
	  io_ns::set_enc_param(enc,i,v);
	}
      }
      else
	io_ns::set_enc_param(enc,pos,v);
      return;
    }
  case def_font_cmd: 
    new_font();
    return;
  case set_interaction_cmd:
    return;
  default: return;
  }
}

// This implements \show. Puts characters in the buffer B, then prints it.
// If what=1, this implements \meaning
// If what=2, this implements \@onelevel@sanitize

void Parser::token_show(int what, Buffer& B)
{
  if(get_token_o()) return;
  bool lg = what==0;
  if(lg) {
    main_ns::log_and_tty << lg_start;
    if(!cur_tok.not_a_cmd())
      main_ns::log_and_tty << cur_tok << "=";
  }
  token_show(lg,cur_cmd_chr,B);
  if(what==2) { // find and strip the prefix
    if(!B.find_char('>')) return;
    int k = B.get_ptr();
    string s = B.to_string(k+1);
    B.reset();
    B.push_back(s);
  }
  if(lg) main_ns::log_and_tty << B.convert_to_log_encoding() << ".\n";
}

// This is for tracingassigns
void Parser::token_show(const CmdChr&val)
{
  static Buffer B;
  B.reset();
  token_show(false,val,B);
  the_log << B.convert_to_log_encoding();
}

// This is the common code
void Parser::token_show(bool lg,const CmdChr&val, Buffer& B)
{
  symcodes K = val.get_cmd();
  if(K==undef_cmd) B << "undefined";
  else if(K>=user_cmd) {
    Macro& X = mac_table.get_macro(val.get_chr());
    if(X.get_type() == dt_optional) B << "opt ";
    B.dump_prefix(false,false,K);
    B << "macro:" << (lg ? " " : "" ) << X;
  } else if(K==set_font_cmd) {
    B << "select font ";
    if(val.get_chr()==0) B << "(null)";
    tfonts.full_name(B,val.get_chr());
  } else if(K>16) {
    String s = val.name();
    if(!s) s = "unknown.";
    B.insert_escape_char_raw(); B << s;
  } else {
    String s = val.special_name();
    if(!s) s = "[unknown command code!]";
    B << s << " " << val.char_val();
  }
}


// Implements \font\foo = cmr10 at 10 pt,
// Or \font\foo = cmr10 scaled 30
// [todo] replace scaled 30 by -10pt
void Parser::new_font ()
{
  Token T = cur_tok;
  int u = get_r_token();
  eq_define(u,CmdChr(set_font_cmd,zero_code),false);
  scan_optional_equals(); 
  string name = scan_file_name();
  int scaled_val =0;
  int at_val =0;
  if(scan_keyword("at")) {
    scan_dimen(false,T);
    at_val =cur_val.get_int_val();
  } else if (scan_keyword("scaled"))
    scaled_val = scan_int(T);
  int res = tfonts.find_font(name,at_val,scaled_val);
  eq_define(u,CmdChr(set_font_cmd,subtypes(res)),false);
}

// Executes equivalent of
// \font\tenrm = cmr10 at 10pt \tenrm"
void Parser::initialise_font()
{
  Token cmd = hash_table.locate("tenrm");
  int u = cmd.eqtb_loc(); 
  int res = tfonts.find_font("cmr10",10<<16,0);
  eq_define(u,CmdChr(set_font_cmd,subtypes(res)),false);
  word_define(cur_font_loc,res,false);
}

// Convert handles \string, \number, \meaning
void Parser::convert() 
{
  int n;
  Token T = cur_tok;
  Buffer B;
  B.reset();
  int c=cur_cmd_chr.get_chr(); 
  switch(c) {
  case number_code: 
  case at_arabic_code:
    n = scan_int(T); 
    B.push_back_int(n); 
    break;
  case twodigits_code:
    { TokenList L = mac_arg(); back_input(L); } // shit
    n = scan_int(T); 
    if(n<10) B.push_back('0');
    B.push_back_int(n); 
    break;
  case romannumeral_code:
    n = scan_int(T);
    B.push_back_roman(n); 
    break;
  case Romannumeral_code:
    n = scan_int(T);
    B.push_back_Roman(n); 
    break;
  case string_code: 
    if(get_token_o()) return; // error ?
    if(cur_tok.not_a_cmd())
      B.push_back(uchar(cur_cmd_chr.get_chr()));
    else {
      uint x = cur_tok.get_val();
      if(x>=cs_token_flag+single_base) 
	B.insert_escape_char_raw();
      if(x>=cs_token_flag+bad_cs)
	B.push_back(hash_table[cur_tok.hash_loc()]); 
      else B.push_back(cur_tok.char_val());
    }
    break;
  case meaning_code: 
    token_show(1,B);
    break;
  case sanitize_code: 
    token_show(2,B);
    break;
  case fontname_code:
    {
      int k = scan_font_ident(); 
      tfonts.full_name(B,k);
      break;
    }
  case jobname_code:
    B.push_back(get_job_name());
    break;
  case ra_jobname_code:
    B.push_back(get_projet_val());
    break;
  case attributeval_code:
    B.push_back(get_attval());
    break;
  case tralicsversion_code:
    B.push_back(the_main->get_version());
    break;
  case etexrevision_code:
    B.push_back(".0");
    break;
  case rayear_code:
    B.push_back_int(the_parser.get_ra_year());
    break;
  }
  TokenList L = B.str_toks(true);
  if(tracing_commands())
    the_log << lg_start << T << "->" << L <<lg_end;
  if(c==sanitize_code) {
    new_macro(L,cur_tok,false);
    return;
  }
  back_input(L);
}


// Kill everything but expr_type
void ScanSlot::kill() 
{
  expr_so_far.kill(); 
  term_so_far.kill();  
  expr_state= se_none;
  term_state = se_none;
  numerator = 0;
} 

// This implements scan_expr, as in eTeX
void Parser::scan_expr(subtypes m)
{
  Token T = cur_tok;
  internal_type it=it_int;
  switch(m) {
  case dimexpr_code: it = it_dimen; break;
  case glueexpr_code: it = it_glue; break;
  case muexpr_code:  it = it_mu; break;
  default: it = it_int;
  }
  //  bool a = arith_ns::seen_error;
  bool b = scan_expr(T,it);
  if(b) {
    parse_error("Arithmetic overflow");
    cur_val.kill();
  }
  //  arith_ns::seen_error = a;
  if(the_parser.tracing_commands())
    the_log <<  lg_startcond << "scan for " << T 
	    << "= " << cur_val << lg_end;
}

// This finds the operator, or reads a termminating \relax
// if the operator stack is not empty, and no operator is given, we expecr
// a closing paren.
scan_expr_t Parser::scan_expr_next(bool stack_empty)
{
  remove_initial_space();
  if(cur_tok.is_plus_token()) return se_add;
  else if(cur_tok.is_minus_token()) return se_sub;
  else if(cur_tok.is_star_token()) return se_mult;
  else if(cur_tok.is_slash_token()) return se_div;
  if(stack_empty) { 
    // read a \relax, push back otherwise
    if(!cur_cmd_chr.is_relax()) back_input();
  }
  else if(!cur_tok.is_close_paren()) {
    parse_error("Expected + - * / or ); closing paren inserted");
    back_input();
  }
  return se_none;
}

// This reads the next item. We have a read a token, and checked that it is
// not an open parenthesis, so that we musg push it back.
void Parser::scan_expr_arg(internal_type x, Token T)
{
  back_input();
  if(x==it_int) scan_int(T);
  else if(x==it_dimen) scan_dimen(false,T);
  else if(x==it_glue) scan_glue(it_glue,T);
  else scan_glue(it_mu,T);
}

// In the case y+x*a*b/c, we handle the two terms y and x*a*b/c
// Consider the second term. When x is read, the state is none, we copy
// x in term_so_far. Quantities a,b,c and integers. When they are read
// we look at what follows (in next_state) and preceeds (in term_state)
// case *a* : we multiply
// case *b/ we remember b in numerator, change next_state to scale
// case /c?: we divide b c
// case \c? : we scale

void ScanSlot::compute_term(scan_expr_t& next_state, SthInternal f,char&C)
{
  C = ' ';
  f.check_overflow(term_state);
  if(term_state == se_none) {
    term_so_far = f;
    if(next_state != se_none) term_so_far.normalise();
  } else if(term_state ==se_mult) {
    if(next_state==se_div) {
      next_state = se_scale;
      numerator = f.get_int_val(); 
    } else {
      C = '*';
      term_so_far.expr_mul(f.get_int_val());
    }
  } else if(term_state ==se_div) {
    C = '/';
    term_so_far.quotient(f.get_int_val());
  }
  else if(term_state ==se_scale) {
    C = '\\';
    term_so_far.scale(numerator,f.get_int_val());
  }
}


// This performs addition or subtraction, or finishes term evaluation 
void ScanSlot::add_or_sub(scan_expr_t& next_state, SthInternal f,char&C)
{
  C = ' ';
  if(next_state> se_sub) {
    term_state = next_state;
    return;
  }
  term_state = se_none; // done with term
  if(expr_state == se_none) {
    C = '=';
    expr_so_far = term_so_far; // convert term to expre
  } else { 
    if(expr_state== se_sub) { C = '-'; term_so_far.negate(); }
    else C = '+';
    expr_so_far.add(term_so_far);
  }
  expr_state = next_state;
}

void Parser::trace_scan_expr(String s, const SthInternal v, char t,Token T)
{
  if(the_parser.tracing_commands() && t != ' ')
    the_log << lg_startcond << s << " so far for " << T 
	    << t << ' ' << v << lg_end;
}

bool Parser::scan_expr(Token T,internal_type et)
{
  vector<ScanSlot> estack;
  ScanSlot W;  
  W.expr_type = et;
  bool b = false; // return value, true in case of overflow
  char tr_state;
  for(;;) {
    // next level is it_int in case we scan a factor,
    internal_type next_level= W.get_next_type();
    remove_initial_space();
    if(cur_tok.is_open_paren()) {
      estack.push_back(W);
      W.kill();
      W.expr_type = next_level;
      continue;
    }
    scan_expr_arg(next_level,T);
    SthInternal f = cur_val;
    for(;;) {   // found here
      scan_expr_t next_state = scan_expr_next(estack.empty());
      //      arith_ns::seen_error = b;
      W.compute_term(next_state, f,tr_state);
      trace_scan_expr("term",W.term_so_far,tr_state,T);
      W.add_or_sub(next_state,f,tr_state);
      trace_scan_expr("expr",W.expr_so_far,tr_state,T);
      //      b = arith_ns::seen_error;
      if(next_state != se_none) break;
      if(estack.empty()) {
	cur_val = W.expr_so_far;
	return b;	
      }
      f = W.expr_so_far;
      W = estack.back();
      estack.pop_back();      
    } 
  }
}


// OK for \font, or \scriptfont, or \nullfont or \tenrm
int Parser::scan_font_ident ()
{
  remove_initial_space();
  if (cur_cmd_chr.get_cmd()==def_font_cmd) 
    return eqtb_int_table[cur_font_loc].get_val();
  else if (cur_cmd_chr.get_cmd()==set_font_cmd) 
    return cur_cmd_chr.get_chr();
  else if (cur_cmd_chr.get_cmd()==def_family_cmd) {
    int a = cur_cmd_chr.get_chr();
    int b = scan_int(cur_tok,15,"family number");
    return eqtb_int_table[a+b].get_val();
  }
  else {
    if(!cur_tok.is_invalid()) back_input();
    parse_error("Missing font identifier");
    return -1;
  }
}

// OK for \mml@font@fraktur, or a font like that or an integer.
int Parser::scan_mathfont_ident ()
{
  remove_initial_space();
  if (cur_cmd_chr.get_cmd()==mathfont_cmd) 
    return cur_cmd_chr.get_chr();
  if (cur_cmd_chr.get_cmd()==math_font_cmd) {
     switch(cur_cmd_chr.get_chr()) {
     case mathtt_code: return math_f_monospace; 
     case mathcal_code: return math_f_script;
     case mathbf_code: return math_f_bold; 
     case mathrm_code: return math_f_upright; 
     case mathfrak_code: return math_f_fraktur;
     case mathit_code: return math_f_italic; 
     case mathnormal_code: return math_f_normal;
     case mathbb_code: return math_f_doublestruck;
     case mathsf_code: return math_f_sansserif; 
     default: break;
     }
  }
  if(!cur_tok.is_invalid()) back_input();
  return scan_int(cur_tok,14,"math font identifier");
}

// This scans a hrule or a vrule.
// Depending on the type, some fields have a default value of 4pt, or 0 pt.
// Others are -\infty (in reality, -2^14pt)
void TexRule::reset()
{
  rule_h = -1073741824;
  rule_w = -1073741824;
  rule_d = -1073741824;
}

void TexRule::convert(AttList&res)
{
  if(rule_h.get_value() != -1073741824)
    res.push_back(np_height,the_main->SH.find_scaled(rule_h));
  if(rule_d.get_value() != -1073741824)
    res.push_back(np_depth,the_main->SH.find_scaled(rule_d));
  if(rule_w.get_value() != -1073741824)
    res.push_back(np_width,the_main->SH.find_scaled(rule_w));
}

void Parser::scan_rule(int c)
{
  TexRule R;
  Token T = cur_tok;
  if(c==rule_code) {
    R.rule_d =0;
    TokenList L1;
    bool b = next_optarg(L1);
    TokenList L2 = mac_arg();
    TokenList L3 = mac_arg();
    c = vrule_code;
    if(b)
      R.rule_d = dimen_from_list(T,L1);
    R.rule_w =  dimen_from_list(T,L2);
    R.rule_h =  dimen_from_list(T,L3);
    R.adjust();
    leave_v_mode();
  }
  else {
    R.reset();
    if(c==vrule_code) R.init_vrule();
    else R.init_hrule();
    for(;;) {
      if(scan_keyword("width")) {
	scan_dimen(false,T);
	R.rule_w = cur_val.get_dim_val();
	continue;
      }
      if(scan_keyword("height")) {
	scan_dimen(false,T);
	R.rule_h = cur_val.get_dim_val();
	continue;
      }
      if(scan_keyword("depth")) {
	scan_dimen(false,T);
	R.rule_d = cur_val.get_dim_val();
	continue;
      }
      break;
    }
  }
  if(c != hrule_code && c != vrule_code) return;
  AttList& res = the_stack.add_newid0(np_rule);
  R.convert(res);
}

// This is the function associated to ' in math mode
void Parser::scan_prime ()
{
  TokenList L;
  for(;;) {
    L.push_back(hash_table.prime_token);
    if(get_token()) break;
    if(cur_cmd_chr.get_cmd()==hat_catcode) {
      TokenList aux = mac_arg();
      L.splice(L.end(),aux);
      break;
    } else if(cur_cmd_chr.get_cmd()==12 && cur_cmd_chr.get_chr()=='\'')
      continue;
    else { 
      back_input(); break;
    }
  }
  brace_me(L);
  back_input(L);
  back_input(hash_table.hat_token);
}

// In order to implement \message properly, we need to know where the cursor is
static int ttypos = 0;
void lg_end(Logger& L)  
{
  ttypos = 0;
  *(L.fp) << "\n";
}

void translate_ns::tex_message(String s)
{
  int k = strlen(s);
  if(ttypos+k>77) { cout << "\n"; ttypos = 0; }
  if(ttypos) { cout << " "; ++ttypos; }
  cout << s;
  ttypos +=k;
}
