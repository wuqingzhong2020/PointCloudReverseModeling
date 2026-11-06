/*
 * 文件描述：以独立构造的圆孔、旋转长圆孔、随机缺点、遮挡和外边缺口验证腹板孔识别。
 * 核对半径过滤、开关、精确圆弧 STEP、命令记录与撤销回放；不启动界面。
 */
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <thread>

#include <gtest/gtest.h>

#include "commands/CommandService.h"
#include "commands/Json.h"
#include "geometry/Geometry.h"

namespace
{
using namespace reverse;
CloudData perforatedScan(bool occlusion = false)
{
    CloudData cloud;
    const auto add=[&](double x,double y,double z){cloud.points.push_back({{x,y,z},0,cloud.points.size()});};
    for(int x=-400;x<=400;x+=2)
    {
        for(int y=-150;y<=150;y+=2)
        {
            const double u=(x-150)*std::cos(0.4)+y*std::sin(0.4);
            const double v=-(x-150)*std::sin(0.4)+y*std::cos(0.4);
            const bool hole=std::hypot(x+250,y+70)<15 || std::hypot(x+250,y-70)<8 ||
                std::hypot(std::max(std::abs(u)-40,0.0),v)<20;
            if(occlusion && std::hypot(x+30,y+70)<20) {add(x,y,80);continue;}
            if(std::hypot(x-395,y-70)<14) continue; // 与外边界相连，不能当作内孔。
            if((x*17+y*29)%997==0) continue; // 零散缺点。
            add(x,y,hole?-40:0); // 孔下背景点不能把孔填掉。
        }
        for(int w=0;w<=10;++w) {add(x,-150+w,100);add(x,140+w,100);}
    }
    return cloud;
}
const RecognizedPlate &web(const PlateRecognition &r) {return r.plates.front();}

TEST(ObservedWebHoles, DetectsRoundAndRotatedSlotWithoutTreatingOcclusionAsHole)
{
    for(bool occluded:{false,true})
    {
        const auto r=recognizePlates(perforatedScan(occluded),BeamEstimate{});
        ASSERT_EQ(web(r).holes.size(),3u);
        std::vector<double> radii;
        std::size_t slots=0;
        for(const auto &h:web(r).holes)
        {
            radii.push_back(h.radiusMm);
            if(h.straightLengthMm>0)
            {
                ++slots;EXPECT_NEAR(h.straightLengthMm,80,5);
                EXPECT_NEAR(std::abs(h.direction.x),std::cos(0.4),0.04);
            }
        }
        std::sort(radii.begin(),radii.end());
        EXPECT_NEAR(radii[0],8,2);EXPECT_NEAR(radii[1],15,2);EXPECT_NEAR(radii[2],20,2);
        EXPECT_EQ(slots,1u);
    }
}

TEST(ObservedWebHoles, RadiusThresholdAndSwitchChangeGeometryAndPreserveSourceCandidates)
{
    const auto r=recognizePlates(perforatedScan(),BeamEstimate{});
    ASSERT_EQ(web(r).holes.size(),3u);
    const auto all=buildRecognizedPlates(r,{false,0,true,0});
    const auto filtered=buildRecognizedPlates(r,{false,0,true,12});
    const auto disabled=buildRecognizedPlates(r,{false,0,false,0});
    EXPECT_EQ(all->summary().plates.front().holes.size(),3u);
    EXPECT_EQ(filtered->summary().plates.front().holes.size(),2u);
    EXPECT_TRUE(disabled->summary().plates.front().holes.empty());
    EXPECT_EQ(web(r).holes.size(),3u);
    EXPECT_EQ(all->summary().solidCount,3u);
    EXPECT_LT(all->summary().volumeMm3,filtered->summary().volumeMm3);
    EXPECT_LT(filtered->summary().volumeMm3,disabled->summary().volumeMm3);
    const auto file=std::filesystem::temp_directory_path()/
        ("observed-web-hole-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".step");
    const auto exported=exportStep(*all,file);
    EXPECT_TRUE(exported.valid);EXPECT_EQ(exported.solidCount,3u);
    std::filesystem::remove(file);
    EXPECT_THROW((void)buildRecognizedPlates(r,{false,0,true,-1}),GeometryError);
    std::stop_source stop;stop.request_stop();
    EXPECT_THROW((void)recognizePlates(perforatedScan(),BeamEstimate{},stop.get_token()),GeometryError);
}

TEST(ObservedWebHoles, OptionsSurviveUiCommandRecordingUndoAndReplay)
{
    const auto prefix=std::filesystem::temp_directory_path()/
        ("web-hole-command-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    auto pcd=prefix;pcd+=".pcd";auto script=prefix;script+=".py";
    const auto cloud=perforatedScan();
    {
        std::ofstream out(pcd);
        out<<"VERSION 0.7\nFIELDS x y z\nSIZE 4 4 4\nTYPE F F F\nCOUNT 1 1 1\nWIDTH "<<cloud.points.size()
           <<"\nHEIGHT 1\nPOINTS "<<cloud.points.size()<<"\nDATA ascii\n";
        for(const auto &p:cloud.points) out<<p.position.x<<' '<<p.position.y<<' '<<p.position.z<<'\n';
    }
    const auto wait=[](CommandService &s,const std::string &id)
    {
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
        while(!s.task(id).finished() && std::chrono::steady_clock::now()<deadline) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        EXPECT_EQ(s.task(id).state,TaskState::Succeeded)<<s.task(id).message;
    };
    {
        CommandService service;
        wait(service,service.submitImport(pcd));
        wait(service,service.submitRecognizedReconstruct(false,service.project().revision,0,true,12));
        ASSERT_TRUE(service.project().model);
        EXPECT_EQ(service.project().model->plates.front().holes.size(),2u);
        wait(service,service.submitStartRecording(script));
        wait(service,service.submitRecognizedReconstruct(false,service.project().revision,0,false,12));
        EXPECT_TRUE(service.project().model->plates.front().holes.empty());
        wait(service,service.submitLine("project.undo"));
        EXPECT_TRUE(service.project().committedRecognitionOptions->recognizeWebHoles);
        EXPECT_DOUBLE_EQ(service.project().committedRecognitionOptions->minimumHoleRadiusMm,12);
        wait(service,service.submitStopRecording());
    }
    {
        CommandService replay;wait(replay,replay.submitScriptFile(script));
        ASSERT_TRUE(replay.project().model);
        EXPECT_EQ(replay.project().model->plates.front().holes.size(),2u);
        EXPECT_EQ(toJson(replay.project())["model"]["recognitionOptions"]["minimumHoleRadiusMm"],12);
    }
    std::filesystem::remove(pcd);std::filesystem::remove(script);
}
}
