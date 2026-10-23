#include "gvn/codecs.h"
#include "test.h"
#include <windows.h>
using namespace gvn;
static std::filesystem::path evidence;
TEST(gif_zero_delay_matches_reference_default_100ms) {
 auto image=load_image(evidence/L"timing/assets/timing.gif");
 CHECK(image.frames.size()==11);
 CHECK(image.frames[1].duration_ms==100);
}
TEST(gif_explicit_one_means_two_passes_in_default_mode) {
 auto image=load_image(evidence/L"loops/assets/loops-1.gif");
 CHECK(image.frames.size()==3);
 CHECK(image.file_loops==2);
}
static void reserved(unsigned method) {
 auto image=load_image(evidence/L"background/assets"/(L"disposal-"+std::to_wstring(method)+L".gif"));
 CHECK(image.width==8&&image.height==4&&image.frames.size()==3);
 for(unsigned y=0;y<4;++y)for(unsigned x=0;x<8;++x)
  CHECK(image.frames[2].pixels[y*8+x]==(x<4?Pixel{20,210,60,255}:Pixel{30,50,220,255}));
}
TEST(gif_reserved_disposal_4_matches_reference_default_keep) {reserved(4);}
TEST(gif_reserved_disposal_5_matches_reference_default_keep) {reserved(5);}
TEST(gif_reserved_disposal_6_matches_reference_default_keep) {reserved(6);}
TEST(gif_reserved_disposal_7_matches_reference_default_keep) {reserved(7);}
TEST(player_honors_two_pass_file_limit_and_holds_last_frame) {
 Image image;image.width=1;image.height=1;image.file_loops=2;
 image.frames={{{Pixel{220,20,40,255}},100},{{Pixel{20,210,60,255}},100}};
 Player player;player.reset(image);player.loop=true;
 player.advance(399);CHECK(player.playing&&player.frame==1);
 player.advance(1);CHECK(!player.playing&&player.frame==1);
}

TEST(gif_all_five_styles_match_reference_durations_and_loop_interpretations) {
 const uint64_t raw[]{1000,0,10,20,30,40,50,90,100,110,200};
 for(int style=0;style<5;++style) {
  LoadingOptions options;options.gif_speed=style;
  const auto timing=load_image(evidence/L"timing/assets/timing.gif",kDecodeLimit,options);
  CHECK(timing.frames.size()==11);
  CHECK(timing.animation_compensate==(style!=2&&style!=3));
  for(size_t i=0;i<11;++i) {
   uint64_t expected=raw[i];
   if(!expected||(style==1&&expected<100)||(style>=2&&expected<20))expected=100;
   CHECK(timing.frames[i].duration_ms==expected);
  }
  for(int loops=-1;loops<=3;++loops) {
   const auto file=loops<0?L"loops-None.gif":L"loops-"+std::to_wstring(loops)+L".gif";
   const auto image=load_image(evidence/L"loops/assets"/file,kDecodeLimit,options);
   const uint32_t expected=loops<0?1:!loops?0:loops+((style==2||style==3)?0:1);
   CHECK(image.file_loops==expected);
  }
 }
}
TEST(gif_all_sixty_reference_background_cases_match_rgb_and_alpha) {
 // This test deliberately flattens onto the reference's known fallback RGB.
 // Independent alpha tests below must also pass before making alpha claims.
 const int map[5][8]={{0,0,2,3,0,0,0,0},{0,0,2,3,3,3,3,3},{0,0,2,3,3,0,0,0},
                       {0,0,2,3,0,0,2,3},{0,0,2,3,3,2,2,2}};
 for(int style=0;style<5;++style)for(int sample=0;sample<12;++sample) {
  LoadingOptions options;options.gif_speed=style;options.preserve_alpha=false;options.background=0x808080;
  const wchar_t* special[]={L"transparent-all",L"transparent-first-only",L"transparent-second-only",L"transparent-first-previous"};
  const auto name=sample<8?L"disposal-"+std::to_wstring(sample):std::wstring(special[sample-8]);
  const int method=sample<8?map[style][sample]:sample==11?3:2;
  const auto image=load_image(evidence/L"background/assets"/(name+L".gif"),kDecodeLimit,options);
  CHECK(image.frames.size()==3&&image.width==8&&image.height==4);
  const Pixel red{220,20,40,255},green{20,210,60,255},blue{30,50,220,255},back{128,128,128,255};
  for(unsigned y=0;y<4;++y)for(unsigned x=0;x<8;++x) {
   CHECK(image.frames[0].pixels[y*8+x]==red);
   CHECK(image.frames[1].pixels[y*8+x]==(x<4?green:red));
   CHECK(image.frames[2].pixels[y*8+x]==(x>=4?blue:method==2?back:method==3?red:green));
  }
 }
}
TEST(gif_reserved_values_preserve_alpha_and_source_bytes) {
 const auto file=evidence/L"background/assets/transparent-all.gif";
 const auto original=read_file(file);
 for(int style=0;style<5;++style) {
  LoadingOptions options;options.gif_speed=style;
  auto bytes=original;
  // Change only the second graphic-control block of this controlled fixture.
  unsigned found=0;
  for(size_t i=0;i+7<bytes.size();++i)if(bytes[i]==0x21&&bytes[i+1]==0xf9&&bytes[i+2]==4)
   if(++found==2){bytes[i+3]=static_cast<uint8_t>((bytes[i+3]&~0x1c)|(6<<2));break;}
  CHECK(found==2);
  const auto saved=bytes;
  const auto image=decode(bytes,L".gif",kDecodeLimit,options);
  CHECK(bytes==saved&&read_file(file)==original);
  const int method=style==1?3:style==3||style==4?2:0;
  for(unsigned y=0;y<4;++y)for(unsigned x=0;x<8;++x) {
   const Pixel value=image.frames[2].pixels[y*8+x];
   if(x>=4)CHECK(value==Pixel{30,50,220,255});
   else if(method==2)CHECK(value.a==0);
   else CHECK(value==(method==3?Pixel{220,20,40,255}:Pixel{20,210,60,255}));
  }
 }
}
TEST(gif_extension_scan_rejects_truncations_and_does_not_match_comment_payloads) {
 const auto original=read_file(evidence/L"loops/assets/loops-1.gif");
 for(size_t size=6;size<original.size();++size)
  THROWS(decode_animation(std::span(original).first(size),kDecodeLimit));
 auto bytes=read_file(evidence/L"loops/assets/loops-None.gif");
 // A syntactically valid comment that contains a fake application extension.
 const std::vector<uint8_t> comment={0x21,0xfe,19,0x21,0xff,11,'N','E','T','S','C','A','P','E','2','.','0',3,1,5,0,0,0};
 bytes.insert(bytes.end()-1,comment.begin(),comment.end());
 CHECK(decode(bytes).file_loops==1);
 LoadingOptions invalid;invalid.gif_speed=5;THROWS(decode(original,L"",kDecodeLimit,invalid));
 invalid.gif_speed=-1;THROWS(decode(original,L"",kDecodeLimit,invalid));
}
TEST(player_finite_infinite_pause_seek_replay_loop_toggle_and_maximum_elapsed) {
 Image image;image.frames={{{},40},{{},100},{{},60}};image.file_loops=2;
 Player p;p.reset(image);p.advance(199);CHECK(p.frame==2&&p.elapsed==59&&p.playing);
 p.advance(1);CHECK(p.frame==0&&p.completed_passes==1&&p.playing);
 p.playing=false;p.advance(UINT64_MAX);CHECK(p.frame==0&&p.completed_passes==1);
 p.play();p.advance(200);CHECK(p.ended&&!p.playing&&p.frame==2);
 p.play();CHECK(p.playing&&!p.ended&&p.frame==0&&p.completed_passes==0);
 p.advance(UINT64_MAX);CHECK(p.ended&&!p.playing&&p.frame==2);
 p.seek(1);p.play();p.loop=false;p.advance(159);CHECK(p.frame==2&&p.playing);
 p.advance(1);CHECK(p.frame==2&&!p.playing&&p.ended);
 p.loop=true;p.play();CHECK(p.frame==0&&p.playing);
 p.advance(100);p.step(-1);CHECK(!p.playing&&p.completed_passes==0&&!p.ended);
 image.file_loops=0;p.reset(image);p.loop=true;p.advance(UINT64_MAX);
 CHECK(p.playing&&p.frame<3&&!p.ended);
 const auto frame=p.frame;const auto elapsed=p.elapsed;
 p.advance(0);CHECK(p.frame==frame&&p.elapsed==elapsed);
 image.animation_compensate=false;image.file_loops=1;p.reset(image);p.advance(UINT64_MAX);
 CHECK(p.frame==1&&p.elapsed==0&&p.playing);
 p.advance(UINT64_MAX);CHECK(p.frame==2&&p.playing);
 p.advance(UINT64_MAX);CHECK(p.frame==2&&!p.playing&&p.ended);
}


TEST(player_can_switch_between_file_limits_infinite_and_current_pass_without_seeking) {
 Image image;image.frames={{{},40},{{},100},{{},60}};image.file_loops=2;
 Player p;p.reset(image);p.advance(200);CHECK(p.completed_passes==1&&p.frame==0);
 p.set_loop(true,false);CHECK(p.completed_passes==0&&!p.honor_file_loops);
 p.advance(UINT64_MAX);CHECK(p.playing&&!p.ended&&p.frame<3);
 auto frame=p.frame;auto elapsed=p.elapsed;
 p.set_loop(true,true);CHECK(p.frame==frame&&p.elapsed==elapsed&&p.completed_passes==0);
 p.advance(400);CHECK(!p.playing&&p.ended&&p.frame==2);
 p.set_loop(true,false);p.play();CHECK(p.frame==0&&p.playing);
 p.advance(39);p.set_loop(false,false);p.advance(160);CHECK(p.playing&&p.frame==2);
 p.advance(1);CHECK(!p.playing&&p.frame==2&&p.ended);
 image.animation_compensate=false;p.reset(image);p.set_loop(true,false);
 for(int i=0;i<12;++i)p.advance(UINT64_MAX);
 CHECK(p.playing&&!p.ended&&p.completed_passes==0);
}


TEST(gif_split_loop_subblocks_preserve_the_library_accepted_loop_count) {
 const auto original=read_file(evidence/L"loops/assets/loops-1.gif");
 const std::vector<uint8_t> marker={0x21,0xff,11,'N','E','T','S','C','A','P','E','2','.','0'};
 auto bytes=original;auto at=std::search(bytes.begin(),bytes.end(),marker.begin(),marker.end());
 CHECK(at!=bytes.end());const auto offset=static_cast<size_t>(at-bytes.begin())+marker.size();
 CHECK(std::vector<uint8_t>(bytes.begin()+offset,bytes.begin()+offset+5)==std::vector<uint8_t>{3,1,1,0,0});
 bytes.erase(bytes.begin()+offset,bytes.begin()+offset+5);
 const std::vector<uint8_t> split={1,1,2,1,0,0};bytes.insert(bytes.begin()+offset,split.begin(),split.end());
 for(int style=0;style<5;++style) {
  LoadingOptions options;options.gif_speed=style;
  const auto image=decode_animation(bytes,kDecodeLimit,options);
  CHECK(image.file_loops==((style==2||style==3)?1:2));
 }
 CHECK(read_file(evidence/L"loops/assets/loops-1.gif")==original);
}
TEST(gif_maximum_loop_value_uses_32bit_total_and_stops_at_the_exact_last_pass) {
 auto bytes=read_file(evidence/L"loops/assets/loops-1.gif");
 const std::vector<uint8_t> marker={0x21,0xff,11,'N','E','T','S','C','A','P','E','2','.','0'};
 auto at=std::search(bytes.begin(),bytes.end(),marker.begin(),marker.end());CHECK(at!=bytes.end());
 const auto offset=static_cast<size_t>(at-bytes.begin())+marker.size();
 bytes[offset+2]=bytes[offset+3]=255;
 for(int style=0;style<5;++style) {
  LoadingOptions options;options.gif_speed=style;const auto image=decode_animation(bytes,kDecodeLimit,options);
  const uint32_t passes=(style==2||style==3)?65535:65536;
  CHECK(image.file_loops==passes);
  Player player;player.reset(image);
  for(uint32_t step=0;step<passes*3-1;++step)player.advance(player.durations[player.frame]);
  CHECK(player.playing&&player.frame==2&&player.completed_passes==passes-1);
  player.advance(player.durations[2]);CHECK(!player.playing&&player.ended&&player.frame==2);
 }
}
TEST(apng_finite_loops_and_excluded_poster_are_unchanged_by_gif_modes_and_can_repeat_infinitely) {
 const auto folder=evidence/L"../../../tests/fixtures/animation-policy";
 for(auto name:{L"finite",L"poster"})for(unsigned plays:{1,2}) {
  const auto file=folder/(std::wstring(name)+L"-"+std::to_wstring(plays)+L".apng");
  const auto original=read_file(file);const auto expected=decode(original);
  CHECK(expected.frames.size()==4&&expected.file_loops==plays);
  for(int style=0;style<5;++style) {
   LoadingOptions options;options.gif_speed=style;const auto image=decode(original,L".apng",kDecodeLimit,options);
   CHECK(image.file_loops==plays&&image.animation_compensate&&image.frames.size()==4);
   for(size_t i=0;i<4;++i)CHECK(image.frames[i].pixels==expected.frames[i].pixels&&image.frames[i].duration_ms==expected.frames[i].duration_ms);
   Player player;player.reset(image);player.advance(player.cycle_ms*plays-1);
   CHECK(player.playing&&player.frame==3);player.advance(1);CHECK(!player.playing&&player.ended);
   player.set_loop(true,false);player.play();player.advance(UINT64_MAX);CHECK(player.playing&&!player.ended);
  }
  CHECK(read_file(file)==original);
 }
}


TEST(gif_loop_data_segmentation_is_preserved_through_public_decode_and_ignores_unknown_apps) {
 const auto original=read_file(evidence/L"loops/assets/loops-1.gif");
 const std::vector<uint8_t> marker={0x21,0xff,11,'N','E','T','S','C','A','P','E','2','.','0'};
 const auto at=std::search(original.begin(),original.end(),marker.begin(),marker.end());CHECK(at!=original.end());
 const auto offset=static_cast<size_t>(at-original.begin())+marker.size();
 const auto expected=decode(original);
 for(const auto& data:{std::vector<uint8_t>{1,1,2,1,0,0},std::vector<uint8_t>{2,1,1,1,0,0},std::vector<uint8_t>{1,1,1,1,1,0,0}}) {
  auto bytes=original;bytes.erase(bytes.begin()+offset,bytes.begin()+offset+5);bytes.insert(bytes.begin()+offset,data.begin(),data.end());
  const auto image=decode(bytes);CHECK(image.file_loops==2&&image.frames.size()==3);
  for(size_t i=0;i<3;++i)CHECK(image.frames[i].pixels==expected.frames[i].pixels);
 }
 auto unknown=original;unknown[static_cast<size_t>(at-original.begin())+3]='X';
 CHECK(decode(unknown).file_loops==1);
}
TEST(gif_duplicate_controls_bad_terminators_and_invalid_canvas_fail_without_output) {
 const auto original=read_file(evidence/L"loops/assets/loops-1.gif");
 const std::vector<uint8_t> marker={0x21,0xff,11,'N','E','T','S','C','A','P','E','2','.','0'};
 const auto at=std::search(original.begin(),original.end(),marker.begin(),marker.end());CHECK(at!=original.end());
 auto duplicate=original;const auto start=at-original.begin();
 // Duplicate a full application extension using a stable separate range.
 duplicate=original;const std::vector<uint8_t> extension(at,at+19);
 duplicate.insert(duplicate.begin()+start,extension.begin(),extension.end());THROWS(decode_animation(duplicate,kDecodeLimit));
 for(const auto& dimensions:{std::pair{0u,64u},std::pair{64u,0u},std::pair{65535u,65535u}}) {
  auto bytes=original;bytes[6]=static_cast<uint8_t>(dimensions.first);bytes[7]=static_cast<uint8_t>(dimensions.first>>8);
  bytes[8]=static_cast<uint8_t>(dimensions.second);bytes[9]=static_cast<uint8_t>(dimensions.second>>8);
  THROWS(decode_animation(bytes,kDecodeLimit));
 }
 const std::vector<uint8_t> control={0x21,0xf9,4};
 const auto gce=std::search(original.begin(),original.end(),control.begin(),control.end());CHECK(gce!=original.end());
 for(auto [field,value]:{std::pair{2u,3u},std::pair{7u,1u}}) {
  auto bytes=original;bytes[static_cast<size_t>(gce-original.begin())+field]=static_cast<uint8_t>(value);
  THROWS(decode_animation(bytes,kDecodeLimit));
 }
 THROWS(decode_animation({},kDecodeLimit));
}
TEST(apng_maximum_png_integer_play_count_finishes_without_iteration_or_integer_wrap) {
 const auto folder=evidence/L"../../../tests/fixtures/animation-policy";
 for(auto name:{L"finite-2147483647.apng",L"poster-2147483647.apng"}) {
  const auto image=load_image(folder/name);CHECK(image.file_loops==0x7fffffffu&&image.frames.size()==4);
  Player player;player.reset(image);player.advance(player.cycle_ms*0x7fffffffu-1);
  CHECK(player.playing&&player.frame==3&&player.completed_passes==0x7ffffffeu);
  player.advance(1);CHECK(!player.playing&&player.ended&&player.completed_passes==0x7fffffffu);
 }
}


TEST(apng_play_counts_above_the_png_integer_range_are_rejected) {
 const auto folder=evidence/L"../../../tests/fixtures/animation-policy";
 for(auto name:{L"finite-2147483648.apng",L"poster-2147483648.apng",L"finite-4294967295.apng",L"poster-4294967295.apng"})
  THROWS(load_image(folder/name));
}


TEST(gif_late_clock_advances_one_frame_and_discards_credit_that_consumes_the_next_frame) {
 Image image;image.format=L"GIF";image.frames={{{},300},{{},100},{{},100},{{},100},{{},100},{{},500}};
 Player player;player.reset(image);player.advance(310);CHECK(player.frame==1&&player.elapsed==10);
 player.advance(370);CHECK(player.frame==2&&player.elapsed==0&&player.playing);
 player.advance(100);CHECK(player.frame==3&&player.elapsed==0);
}
TEST(gif_clock_compensates_only_credit_inside_the_next_frame_and_firefox_discards_it) {
 Image image;image.format=L"GIF";image.frames={{{},300},{{},100},{{},300},{{},100},{{},100},{{},500}};
 Player player;player.reset(image);player.advance(310);player.advance(370);
 CHECK(player.frame==2&&player.elapsed==280);player.advance(20);CHECK(player.frame==3&&player.elapsed==0);
 image.animation_compensate=false;player.reset(image);player.advance(310);player.advance(370);
 CHECK(player.frame==2&&player.elapsed==0);player.advance(299);CHECK(player.frame==2);
 player.advance(1);CHECK(player.frame==3);
}
TEST(gif_delayed_timer_still_displays_each_frame_before_the_finite_end) {
 Image image;image.format=L"GIF";image.file_loops=1;image.frames={{{},100},{{},100},{{},100}};
 Player player;player.reset(image);player.advance(1000);
 CHECK(player.frame==1&&player.playing&&!player.ended);
 player.advance(100);CHECK(player.frame==2&&player.playing);
 player.advance(100);CHECK(player.frame==2&&!player.playing&&player.ended);
}


TEST(gif_credit_equal_to_next_duration_is_discarded_and_zero_elapsed_does_not_advance) {
 Image image;image.format=L"GIF";image.frames={{{},100},{{},100},{{},100}};
 Player player;player.reset(image);player.advance(100);player.advance(200);
 CHECK(player.frame==2&&player.elapsed==0&&player.playing);
 CHECK(!player.advance(0)&&player.frame==2&&player.playing);
 player.advance(100);CHECK(player.frame==0&&player.elapsed==0&&player.playing);
}

int wmain(int argc,wchar_t**argv) {
 if(argc!=2)return 2;
 evidence=argv[1];
 HRESULT hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
 if(FAILED(hr))return 2;
 int result=run_tests();CoUninitialize();return result;
}
