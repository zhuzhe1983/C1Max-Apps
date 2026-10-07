#include "text_input.hpp"
#include "display.hpp"
#include "net.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <unistd.h>

namespace c1ime {
namespace {
constexpr uint32_t bg=0x102328,panel=0x19353a,ink=0xf5f4e9,muted=0xb3cbc5,accent=0x95d5be;
lv_obj_t* label(lv_obj_t* p,const std::string& value,int x,int y,int w,uint32_t color=ink){
    auto*o=lv_label_create(p);lv_obj_set_pos(o,x,y);lv_obj_set_width(o,w);lv_label_set_long_mode(o,LV_LABEL_LONG_DOT);
    lv_label_set_text(o,value.c_str());lv_obj_set_style_text_color(o,lv_color_hex(color),0);return o;
}
lv_obj_t* button(lv_obj_t*p,const std::string&value,int x,int y,int w,int h,lv_event_cb_t cb,void*arg){
    auto*o=lv_button_create(p);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);lv_obj_set_style_bg_color(o,lv_color_hex(panel),0);
    lv_obj_set_style_border_color(o,lv_color_hex(0x48645e),0);lv_obj_set_style_border_width(o,1,0);lv_obj_set_style_radius(o,8,0);
    lv_obj_set_style_shadow_width(o,0,0);lv_obj_set_style_pad_all(o,2,0);
    auto*l=label(o,value,0,0,w-10);lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,0);lv_obj_center(l);
    lv_obj_add_event_cb(o,cb,LV_EVENT_CLICKED,arg);return o;
}
}
TextInput::~TextInput(){close();}
void TextInput::close(){
    if(root_){
        FILE*f=std::fopen("/tmp/c1max-ime.pid","r");long pid=0;
        if(f){if(std::fscanf(f,"%ld",&pid)!=1)pid=0;std::fclose(f);}
        if(pid==getpid())unlink("/tmp/c1max-ime.pid");
    }
    apply_={};if(root_)lv_obj_delete(root_);root_=field_=mode_=preedit_=choices_=hint_=nullptr;
    engine_.reset();error_.clear();
}
std::string TextInput::text()const{return field_?lv_textarea_get_text(field_):"";}
std::string TextInput::preedit()const{return engine_?engine_->buffer():"";}
std::vector<Candidate> TextInput::candidates()const{return engine_?engine_->candidates():std::vector<Candidate>{};}
void TextInput::open(const std::string&app,const std::string&title,const std::string&value,unsigned maximum,const lv_font_t*font,std::function<void(std::string)>apply,bool multiline,unsigned max_bytes){
    close();apply_=std::move(apply);max_bytes_=max_bytes;
    // Let the session volume helper reserve Shift+volume for candidate paging.
    c1::save_private("/tmp/c1max-ime.pid",std::to_string(getpid())+"\n");
    root_=lv_obj_create(lv_layer_top());lv_obj_set_pos(root_,0,0);lv_obj_set_size(root_,800,340);lv_obj_remove_flag(root_,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(root_,0,0);lv_obj_set_style_border_width(root_,0,0);lv_obj_set_style_radius(root_,0,0);
    lv_obj_set_style_bg_color(root_,lv_color_hex(bg),0);lv_obj_set_style_bg_opa(root_,LV_OPA_COVER,0);if(font)lv_obj_set_style_text_font(root_,font,0);
    label(root_,title,20,12,520,accent);
    auto*b=button(root_,"拼音",615,7,165,34,[](lv_event_t*e){static_cast<TextInput*>(lv_event_get_user_data(e))->toggle();},this);mode_=lv_obj_get_child(b,0);
    field_=lv_textarea_create(root_);lv_obj_set_pos(field_,20,52);lv_obj_set_size(field_,760,76);lv_textarea_set_one_line(field_,!multiline);
    lv_textarea_set_max_length(field_,std::clamp(maximum,1u,4096u));lv_textarea_set_text(field_,value.c_str());lv_textarea_set_cursor_pos(field_,LV_TEXTAREA_CURSOR_LAST);
    lv_obj_set_style_bg_color(field_,lv_color_hex(panel),0);lv_obj_set_style_text_color(field_,lv_color_hex(ink),0);
    lv_obj_set_style_border_color(field_,lv_color_hex(accent),0);lv_obj_set_style_radius(field_,8,0);lv_obj_set_style_pad_all(field_,12,0);lv_obj_add_state(field_,LV_STATE_FOCUSED);
    preedit_=label(root_,"",22,137,552,accent);
    button(root_,"‹",626,134,70,28,[](lv_event_t*e){static_cast<TextInput*>(lv_event_get_user_data(e))->turn(-1);},this);
    button(root_,"›",707,134,70,28,[](lv_event_t*e){static_cast<TextInput*>(lv_event_get_user_data(e))->turn(1);},this);
    choices_=lv_obj_create(root_);lv_obj_remove_style_all(choices_);lv_obj_remove_flag(choices_,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_pos(choices_,20,172);lv_obj_set_size(choices_,760,74);
    hint_=label(root_,"",22,255,756,muted);
    button(root_,"返回取消",20,292,178,38,[](lv_event_t*e){static_cast<TextInput*>(lv_event_get_user_data(e))->close();},this);
    button(root_,"确认输入",603,292,177,38,[](lv_event_t*e){static_cast<TextInput*>(lv_event_get_user_data(e))->confirm();},this);
    label(root_,"拍摄键：中/英 · 双击 Shift：大小写",210,301,385,muted);
    engine_=std::make_unique<Engine>(c1::root()+"/"+app+"/assets/rime-data",c1::data()+"/"+app+"/rime");
    if(engine_->initialize())engine_->toggle_mode();else{error_="拼音暂不可用，请检查应用词库";engine_.reset();}
    refresh();
}
void TextInput::refresh(){
    if(!root_)return;bool chinese=engine_&&engine_->mode()==Mode::Chinese;
    lv_label_set_text(mode_,chinese?"拼音 / English":screen::caps_lock()?"ABC / 拼音":"abc / 拼音");
    auto composition=preedit();lv_label_set_text(preedit_,composition.empty()?(chinese?"输入拼音，选择汉字":"英文 / 数字 / 符号"):composition.c_str());
    lv_label_set_text(hint_,error_.empty()?"空格/回车选词 · Shift＋数字选词 · Shift＋音量 ± 翻候选":error_.c_str());
    lv_obj_clean(choices_);auto list=candidates();if(!chinese)return;
    for(size_t i=0;i<list.size()&&i<9;i++){
        auto*b=button(choices_,std::to_string(i+1)+" "+list[i].text,int(i%5)*152,int(i/5)*38,146,34,[](lv_event_t*e){auto*self=static_cast<TextInput*>(lv_event_get_user_data(e));auto*target=static_cast<lv_obj_t*>(lv_event_get_target(e));self->choose(lv_obj_get_index(target));},this);(void)b;
    }
}
void TextInput::toggle(){if(engine_)engine_->toggle_mode();refresh();}
void TextInput::choose(int index){if(!engine_)return;auto s=engine_->select(index);if(!s.empty())lv_textarea_add_text(field_,s.c_str());refresh();}
void TextInput::confirm(){
    if(engine_&&engine_->state()!=State::Inactive){choose(0);return;}
    auto value=text();if(max_bytes_&&value.size()>max_bytes_){error_="文字太长：最多 "+std::to_string(max_bytes_)+" UTF-8 字节，请缩短后确认";refresh();return;}auto apply=std::move(apply_);close();if(apply)apply(std::move(value));
}
void TextInput::turn(int direction){if(engine_){if(direction<0)engine_->page_up();else engine_->page_down();}refresh();}
bool TextInput::key(uint32_t k){
    if(!active())return false;
    if(k==screen::KEY_HOME){close();return false;}
    if(k==screen::KEY_SYMBOL){toggle();return true;}
    if(k==screen::KEY_MODE){refresh();return true;}
    bool chinese=engine_&&engine_->mode()==Mode::Chinese, composing=engine_&&engine_->state()!=State::Inactive;
    if(k==screen::KEY_EXIT){if(composing){engine_->cancel();refresh();}else close();return true;}
    if(k==screen::KEY_FONT_UP||k==screen::KEY_FONT_DOWN){turn(k==screen::KEY_FONT_UP?-1:1);return true;}
    if(composing){
        if(k==LV_KEY_BACKSPACE){engine_->backspace();refresh();return true;}
        if(k==LV_KEY_ENTER||k==' '){choose(0);return true;}
        if(k>='1'&&k<='9'){choose(k-'1');return true;}
        if(k==LV_KEY_UP||k==LV_KEY_LEFT){turn(-1);return true;}
        if(k==LV_KEY_DOWN||k==LV_KEY_RIGHT){turn(1);return true;}
    }
    if(k==LV_KEY_ENTER){confirm();return true;}
    if(k==LV_KEY_BACKSPACE){lv_textarea_delete_char(field_);return true;}
    if(k==LV_KEY_LEFT){lv_textarea_cursor_left(field_);return true;}if(k==LV_KEY_RIGHT){lv_textarea_cursor_right(field_);return true;}
    if(chinese&&k>=32&&k<127){char c=char(k);if(c>='A'&&c<='Z')c+=32;if(engine_->input(c)){auto s=engine_->take_commit();if(!s.empty())lv_textarea_add_text(field_,s.c_str());refresh();return true;}}
    if(k>=32&&k<127){char s[]={char(k),0};lv_textarea_add_text(field_,s);}return true;
}
}
