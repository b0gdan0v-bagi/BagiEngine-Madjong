#include <Mahjong/Arena/ArenaCli.h>
#include <gtest/gtest.h>
#include <thread>

/**
 * The command line of the arena (`src/Mahjong/Arena/ArenaCli.h`, the program `BagiMahjongArena`): the arguments (defaults, every option,
 * a list of focus players with knobs, every refusal with its message), and the whole program on a few seeds: the report, twice the same
 * text, `--help`, the exit codes, `--time`. Pure logic: no engine services; the file takes about 1.7 s in Debug (three seeds).
 */
namespace BECore::Mahjong::Arena {

    namespace {

        constexpr int kFewSeeds = 3;
        constexpr uint32_t kFirstSeed = 4000;
        constexpr uint32_t kOtherFirstSeed = 17;

        using Args = eastl::vector<std::string_view>;

        std::expected<ArenaArgs, eastl::string> Parse(const Args& args) {
            return ParseArenaArgs(eastl::span<const std::string_view>(args.data(), args.size()));
        }

        std::string ErrorOf(const Args& args) {
            const std::expected<ArenaArgs, eastl::string> parsed = Parse(args);
            EXPECT_FALSE(parsed.has_value());
            return parsed.has_value() ? std::string() : std::string(parsed.error().c_str());
        }

        ArenaArgs ArgsOf(const Args& args) {
            const std::expected<ArenaArgs, eastl::string> parsed = Parse(args);
            EXPECT_TRUE(parsed.has_value()) << (parsed.has_value() ? "" : parsed.error().c_str());
            return parsed.has_value() ? *parsed : ArenaArgs{};
        }

        int RunProgram(const Args& args, ArenaCliOutput& output) {
            return RunArenaCli(eastl::span<const std::string_view>(args.data(), args.size()), output);
        }

        bool Contains(const eastl::string& text, std::string_view part) {
            return std::string_view(text.data(), text.size()).find(part) != std::string_view::npos;
        }

        /// The option and the count of seeds that every run of the program in this file uses.
        Args FewSeeds(std::initializer_list<std::string_view> more = {}) {
            Args args{"--seeds", "3"};
            for (const std::string_view argument : more) {
                args.push_back(argument);
            }
            return args;
        }

    }  // namespace

    TEST(ArenaCliTest, NoArgumentsAreTheDefaults) {
        const ArenaArgs parsed = ArgsOf({});
        EXPECT_FALSE(parsed.help);
        EXPECT_FALSE(parsed.config.timing);
        EXPECT_EQ(parsed.config.first, kDefaultFirstSeed);
        EXPECT_EQ(parsed.config.first, kFirstSeed);
        EXPECT_EQ(parsed.config.count, kDefaultSeedCount);
        ASSERT_EQ(parsed.config.focus.size(), size_t{1});
        EXPECT_EQ(parsed.config.focus[0].name, "normal");
        EXPECT_FALSE(parsed.config.focus[0].random);
        EXPECT_TRUE(parsed.config.others.random);
        EXPECT_EQ(parsed.config.others.name, "random");
    }

    TEST(ArenaCliTest, EveryOptionIsRead) {
        const ArenaArgs parsed = ArgsOf({"--focus", "easy", "--others", "normal", "--first", "17", "--seeds", "3", "--time"});
        ASSERT_EQ(parsed.config.focus.size(), size_t{1});
        EXPECT_EQ(parsed.config.focus[0].name, "easy");
        EXPECT_FALSE(parsed.config.others.random);
        EXPECT_EQ(parsed.config.others.name, "normal");
        EXPECT_EQ(parsed.config.first, kOtherFirstSeed);
        EXPECT_EQ(parsed.config.count, kFewSeeds);
        EXPECT_TRUE(parsed.config.timing);
        EXPECT_FALSE(parsed.help);
        EXPECT_EQ(parsed.config.jobs, kDefaultJobs);
        EXPECT_EQ(ArgsOf({"--jobs", "4"}).config.jobs, 4);
        EXPECT_EQ(ArgsOf({"--jobs", "256"}).config.jobs, kMaxJobs);
    }

    TEST(ArenaCliTest, JobsZeroIsTheHardwareConcurrency) {
        const int jobs = ArgsOf({"--jobs", "0"}).config.jobs;
        EXPECT_GE(jobs, 1);
        EXPECT_LE(jobs, kMaxJobs);
        const unsigned cores = std::thread::hardware_concurrency();
        if (cores >= 1 && cores <= static_cast<unsigned>(kMaxJobs)) {
            EXPECT_EQ(jobs, static_cast<int>(cores));
        }
    }

    TEST(ArenaCliTest, TheFocusListHoldsSeveralPlayersAndKnobs) {
        const ArenaArgs plain = ArgsOf({"--focus", "normal,easy,normal,random"});
        ASSERT_EQ(plain.config.focus.size(), size_t{4});
        EXPECT_EQ(plain.config.focus[0].name, "normal");
        EXPECT_EQ(plain.config.focus[3].name, "random");
        EXPECT_TRUE(plain.config.focus[3].random);
        // A part that sets a knob (no colon) goes on the spec before it; a part with a colon, or a level, starts a new player.
        const ArenaArgs knobs = ArgsOf({"--focus", "normal,easy:skipClaimPercent=20,skipDeclarationPercent=10,easy,normal:skipAcceptancePercent=5"});
        ASSERT_EQ(knobs.config.focus.size(), size_t{4});
        EXPECT_EQ(knobs.config.focus[1].name, "easy:skipClaimPercent=20,skipDeclarationPercent=10");
        EXPECT_EQ(knobs.config.focus[1].profile.skipClaimPercent, 20);
        EXPECT_EQ(knobs.config.focus[1].profile.skipDeclarationPercent, 10);
        EXPECT_EQ(knobs.config.focus[2].name, "easy");
        EXPECT_EQ(knobs.config.focus[3].profile.skipAcceptancePercent, 5);
    }

    TEST(ArenaCliTest, HelpWinsOverTheRest) {
        EXPECT_TRUE(ArgsOf({"--help"}).help);
        EXPECT_TRUE(ArgsOf({"--seeds", "5", "--help"}).help);
        // Everything after --help is not looked at, so the usage is always available.
        EXPECT_TRUE(ArgsOf({"--help", "--nonsense"}).help);
    }

    TEST(ArenaCliTest, BadArgumentsAreRefusedWithAMessageThatNamesThem) {
        EXPECT_NE(ErrorOf({"--verbose"}).find("unknown argument \"--verbose\""), std::string::npos);
        EXPECT_NE(ErrorOf({"normal"}).find("unknown argument \"normal\""), std::string::npos);
        EXPECT_NE(ErrorOf({"--focus"}).find("--focus needs a value"), std::string::npos);
        EXPECT_NE(ErrorOf({"--others"}).find("--others needs a value"), std::string::npos);
        EXPECT_NE(ErrorOf({"--first"}).find("--first needs a value"), std::string::npos);
        EXPECT_NE(ErrorOf({"--seeds"}).find("--seeds needs a value"), std::string::npos);
        EXPECT_NE(ErrorOf({"--jobs"}).find("--jobs needs a value"), std::string::npos);
        EXPECT_NE(ErrorOf({"--jobs", "many"}).find("--jobs: \"many\" is not a whole number from 0 to 256"), std::string::npos);
        EXPECT_NE(ErrorOf({"--jobs", "-1"}).find("is not a whole number from 0 to 256"), std::string::npos);
        EXPECT_NE(ErrorOf({"--jobs", "257"}).find("is not a whole number from 0 to 256"), std::string::npos);
        EXPECT_NE(ErrorOf({"--jobs", "2x"}).find("--jobs:"), std::string::npos);
        EXPECT_NE(ErrorOf({"--seeds", "0"}).find("at least 1"), std::string::npos);
        EXPECT_NE(ErrorOf({"--seeds", "-5"}).find("at least 1"), std::string::npos);
        EXPECT_NE(ErrorOf({"--seeds", "many"}).find("--seeds: \"many\" is not a whole number"), std::string::npos);
        EXPECT_NE(ErrorOf({"--seeds", "3x"}).find("is not a whole number"), std::string::npos);
        EXPECT_NE(ErrorOf({"--first", "-1"}).find("--first: \"-1\" is not a whole number"), std::string::npos);
        EXPECT_NE(ErrorOf({"--first", "4294967296"}).find("is not a whole number"), std::string::npos);
        EXPECT_NE(ErrorOf({"--focus", "Normal"}).find("--focus: unknown player \"Normal\""), std::string::npos);
        EXPECT_NE(ErrorOf({"--focus", "normal,"}).find("empty player spec"), std::string::npos);
        EXPECT_NE(ErrorOf({"--focus", "easy:skipClaimPercent=200"}).find("outside 0..100"), std::string::npos);
        EXPECT_NE(ErrorOf({"--focus", "skipClaimPercent=20"}).find("unknown player"), std::string::npos);
        EXPECT_NE(ErrorOf({"--others", "medium"}).find("--others: unknown player \"medium\""), std::string::npos);
    }

    TEST(ArenaCliTest, ARangeThatRunsPastTheLargestSeedIsRefused) {
        EXPECT_TRUE(Parse({"--first", "4294967295", "--seeds", "1"}).has_value());
        EXPECT_NE(ErrorOf({"--first", "4294967295", "--seeds", "2"}).find("run past the largest seed"), std::string::npos);
    }

    TEST(ArenaCliTest, TheUsageSaysTheDefaultsAndTheIntervalRule) {
        const eastl::string usage = ArenaUsage();
        for (const std::string_view option : {"--focus", "--others", "--first", "--seeds", "--jobs", "--time", "--help"}) {
            EXPECT_TRUE(Contains(usage, option)) << std::string(option);
        }
        EXPECT_TRUE(Contains(usage, "default 4000"));
        EXPECT_TRUE(Contains(usage, "default 250"));
        EXPECT_TRUE(Contains(usage, "default normal"));
        EXPECT_TRUE(Contains(usage, "default random"));
        EXPECT_TRUE(Contains(usage, "1.96 standard errors"));
        EXPECT_TRUE(Contains(usage, "the seed is the sample"));
    }

    TEST(ArenaCliTest, HelpPrintsTheUsageAndExitsOk) {
        ArenaCliOutput output;
        EXPECT_EQ(RunProgram({"--help"}, output), kExitOk);
        EXPECT_EQ(output.out, ArenaUsage());
        EXPECT_TRUE(output.err.empty());
    }

    TEST(ArenaCliTest, ABadArgumentExitsWithTwoAndPlaysNothing) {
        ArenaCliOutput output;
        EXPECT_EQ(RunProgram({"--seeds", "0"}, output), kExitBadArguments);
        EXPECT_EQ(kExitBadArguments, 2);
        EXPECT_TRUE(output.out.empty());
        EXPECT_TRUE(Contains(output.err, "error: the number of seeds is 0, it must be at least 1")) << output.err.c_str();
        // The usage follows the message.
        EXPECT_TRUE(Contains(output.err, "usage: BagiMahjongArena")) << output.err.c_str();
        EXPECT_FALSE(Contains(output.err, "Bot arena"));
        // A bad number of threads is the same: exit 2, the message, the usage, nothing played.
        ArenaCliOutput jobs;
        EXPECT_EQ(RunProgram({"--jobs", "300"}, jobs), kExitBadArguments);
        EXPECT_TRUE(jobs.out.empty());
        EXPECT_TRUE(Contains(jobs.err, "error: --jobs: \"300\" is not a whole number from 0 to 256")) << jobs.err.c_str();
        EXPECT_TRUE(Contains(jobs.err, "usage: BagiMahjongArena"));
    }

    TEST(ArenaCliTest, ARunPrintsTheReport) {
        ArenaCliOutput output;
        EXPECT_EQ(RunProgram(FewSeeds(), output), kExitOk);
        EXPECT_TRUE(output.err.empty());
        const eastl::string& text = output.out;
        EXPECT_TRUE(Contains(text, "Bot arena: seeds 4000 .. 4002 (3 seeds, 12 hands per focus player)")) << text.c_str();
        EXPECT_TRUE(Contains(text, "focus: normal; others: random"));
        EXPECT_TRUE(Contains(text, "avg win score"));
        EXPECT_TRUE(Contains(text, "others (per seat)"));
        // One focus player: no paired table, no timing table.
        EXPECT_FALSE(Contains(text, "paired difference"));
        EXPECT_FALSE(Contains(text, "per decision"));
        EXPECT_FALSE(Contains(text, "wall clock"));
    }

    TEST(ArenaCliTest, TheSameArgumentsGiveTheSameBytes) {
        ArenaCliOutput first;
        ArenaCliOutput second;
        EXPECT_EQ(RunProgram(FewSeeds({"--focus", "normal,easy"}), first), kExitOk);
        EXPECT_EQ(RunProgram(FewSeeds({"--focus", "normal,easy"}), second), kExitOk);
        EXPECT_FALSE(first.out.empty());
        EXPECT_EQ(first.out, second.out);
    }

    TEST(ArenaCliTest, SeveralFocusPlayersGetThePairedTable) {
        ArenaCliOutput output;
        EXPECT_EQ(RunProgram(FewSeeds({"--focus", "normal,easy:skipClaimPercent=20", "--others", "normal", "--first", "17"}), output), kExitOk);
        EXPECT_TRUE(Contains(output.out, "seeds 17 .. 19")) << output.out.c_str();
        EXPECT_TRUE(Contains(output.out, "focus: normal, easy:skipClaimPercent=20; others: normal"));
        EXPECT_TRUE(Contains(output.out, "paired difference against normal (the same seeds)"));
    }

    TEST(ArenaCliTest, TheThreadsDoNotChangeTheBytes) {
        ArenaCliOutput single;
        EXPECT_EQ(RunProgram(FewSeeds({"--focus", "normal,easy", "--jobs", "1"}), single), kExitOk);
        EXPECT_FALSE(single.out.empty());
        for (const std::string_view jobs : {"2", "5", "0"}) {
            ArenaCliOutput threaded;
            EXPECT_EQ(RunProgram(FewSeeds({"--focus", "normal,easy", "--jobs", jobs}), threaded), kExitOk) << std::string(jobs);
            EXPECT_EQ(threaded.out, single.out) << "jobs " << std::string(jobs);
            EXPECT_TRUE(threaded.err.empty());
        }
    }

    TEST(ArenaCliTest, TimeAddsTheTimingTable) {
        ArenaCliOutput timed;
        EXPECT_EQ(RunProgram(FewSeeds({"--time"}), timed), kExitOk);
        EXPECT_TRUE(Contains(timed.out, "wall clock, not deterministic")) << timed.out.c_str();
        EXPECT_TRUE(Contains(timed.out, "us per decision"));
        ArenaCliOutput quiet;
        EXPECT_EQ(RunProgram(FewSeeds(), quiet), kExitOk);
        EXPECT_FALSE(Contains(quiet.out, "us per decision"));
    }

}  // namespace BECore::Mahjong::Arena
