/**
 * LOST starting point
 *
 * Reads in CLI arguments/flags and starts the appropriate pipelines
 */

#include <assert.h>
#include <sys/types.h>
#include <unistd.h>
#include <getopt.h>

#include <bitset>
#include <string>
#include <iostream>
#include <fstream>
#include <chrono>
#include <cstring>
#include <map>
#include <deque>


#include "databases.hpp"
#include "centroiders.hpp"
#include "decimal.hpp"
#include "io.hpp"
#include "man-database.h"
#include "man-pipeline.h"
#include "star-id.hpp"
#include "star-utils.hpp"


#include <SFML/Graphics.hpp>

#include <sfml-utils.hpp>





namespace lost {

/// Create a database and write it to a file based on the command line options in \p values
static void DatabaseBuild(const DatabaseOptions &values) {
    Catalog narrowedCatalog = NarrowCatalog(CatalogRead(), (int) (values.minMag * 100), values.maxStars, DegToRad(values.minSeparation));
    std::cerr << "Narrowed catalog has " << narrowedCatalog.size() << " stars." << std::endl;

    MultiDatabaseDescriptor dbEntries = GenerateDatabases(narrowedCatalog, values);
    SerializeContext ser = serFromDbValues(values);

    // Create & Set Flags.
    uint32_t dbFlags = 0;
    dbFlags |= typeid(decimal) == typeid(float) ? MULTI_DB_FLOAT_FLAG : 0;

    // Serialize Flags
    SerializeMultiDatabase(&ser, dbEntries, dbFlags);

    std::cerr << "Generated database with " << ser.buffer.size() << " bytes" << std::endl;
    std::cerr << "Database flagged with " << std::bitset<8*sizeof(dbFlags)>(dbFlags) << std::endl;

    UserSpecifiedOutputStream pos = UserSpecifiedOutputStream(values.outputPath, true);
    pos.Stream().write((char *) ser.buffer.data(), ser.buffer.size());

}

/// Run a star-tracking pipeline (possibly including generating inputs and analyzing outputs) based on command line options in \p values.
static void PipelineRun(const PipelineOptions &values) {
    PipelineInputList input = GetPipelineInput(values);
    Pipeline pipeline = SetPipeline(values);
    std::vector<PipelineOutput> outputs = pipeline.Go(input);
    PipelineComparison(input, outputs, values);
}

static std::vector<dost_ImgData> PipelineRunSFML(PipelineOptions &values) {
    std::vector<dost_ImgData> returnData;


    // Force generation mode
    values.generate = 1;

    // Ensure at least one frame
    if (values.frames < 1) values.frames = 1;

    // If max is not set, set it to min. Obviously, if the user really wants to tween to zero, this may be problematic, but min = 0 and max >= 0 is a reasonable assumption.
    if (values.rollMax == 0) values.rollMax = values.rollMin;
    if (values.raMax == 0)   values.raMax   = values.raMin;
    if (values.decMax == 0)  values.decMax  = values.decMin;


    // Force certain algorithms for current testing purposes.
    values.centroidAlgo = "cog";
    values.idAlgo = "py";
    values.attitudeAlgo = "dqm";
    values.databasePath = "my-database.dat";

    // Set up Pipeline and reserve space for each frame.
    Pipeline pipeline = SetPipeline(values); 

    returnData.reserve(values.panning ? 1 : values.frames);


    int startFrame = 0;
    if (values.panning) {
        startFrame = values.frames - 1;
    }

    // Generate frame by frame.
    for (int frame = startFrame; frame < values.frames; frame++) {
        std::cout << "Processing frame: " << frame << "\n";

        // Logic for interpolation (works for both cases because if panning, Min==Max)
        double t = (values.frames > 1) ? (double)frame / (values.frames - 1) : 0.0;

        values.generateRoll = values.rollMin + t * (values.rollMax - values.rollMin);
        values.generateRa   = values.raMin   + t * (values.raMax   - values.raMin);
        values.generateDe   = values.decMin  + t * (values.decMax  - values.decMin);

        // Naming convention
        char buffer[256];
        snprintf(buffer, sizeof(buffer), "sfml-tests/frame_%04d.png", frame);
        values.plotRawInput = std::string(buffer);

        // Run Pipeline
        PipelineInputList input = GetPipelineInput(values);
        std::vector<PipelineOutput> outputs = pipeline.Go(input);

        if (outputs.empty()) continue;

        const auto& out = outputs[0];
        dost_ImgData imgData;
        
        if (out.attitude) imgData.attitude = *out.attitude;
        if (out.stars)    imgData.stars    = *out.stars;

        if (out.starIds && !out.catalog.empty()) {
            for (const StarIdentifier &id : *out.starIds) {
                imgData.starIds.emplace_back(id.starIndex, id.catalogIndex);
            }
        }

        returnData.push_back(imgData);

        // Only print comparison for the frame being generated
        PipelineComparison(input, outputs, values); 
    }

    return returnData;
}


// DO NOT DELETE
// static void PipelineBenchmark() {
//     PipelineInputList input = PromptPipelineInput();
//     Pipeline pipeline = PromptPipeline();
//     int iterations = Prompt<int>("Times to run the pipeline");
//     std::cerr << "Benchmarking..." << std::endl;

//     // TODO: we can do better than this :| maybe include mean time, 99% time, or allow a vector of
//     // input and determine which one took the longest
//     auto startTime = std::chrono::high_resolution_clock::now();
//     for (int i = 0; i < iterations; i++) {
//         pipeline.Go(input);
//     }
//     auto endTime = std::chrono::high_resolution_clock::now();
//     auto totalTime = std::chrono::duration<double, std::milli>(endTime - startTime);
//     std::cout << "total_ms " << totalTime.count() << std::endl;
// }

// static void EstimateCamera() {
//     std::cerr << "Enter estimated camera details when prompted." << std::endl;
//     PipelineInputList inputs = PromptPngPipelineInput();
//     float baseFocalLength = inputs[0]->InputCamera()->FocalLength();
//     float deviationIncrement = Prompt<float>("Focal length increment (base: " + std::to_string(baseFocalLength) + ")");
//     float deviationMax = Prompt<float>("Maximum focal length deviation to attempt");
//     Pipeline pipeline = PromptPipeline();

//     while (inputs[0]->InputCamera()->FocalLength() - baseFocalLength <= deviationMax) {
//         std::cerr << "Attempt focal length " << inputs[0]->InputCamera()->FocalLength() << std::endl;
//         std::vector<PipelineOutput> outputs = pipeline.Go(inputs);
//         if (outputs[0].nice) {
//             std::cout << "camera_identified true" << std::endl << *inputs[0]->InputCamera();
//             return;
//         }

//         Camera camera(*inputs[0]->InputCamera());
//         if (camera.FocalLength() - baseFocalLength > 0) {
//             // yes i know this expression can be simplified shut up
//             camera.SetFocalLength(camera.FocalLength() - 2*(camera.FocalLength() - baseFocalLength));
//         } else {
//             camera.SetFocalLength(camera.FocalLength() + 2*(baseFocalLength - camera.FocalLength()) + deviationIncrement);
//         }
//         ((PngPipelineInput *)(inputs[0].get()))->SetCamera(camera);
//     }
//     std::cout << "camera_identified false" << std::endl;
// }

/// Convert string to boolean
bool atobool(const char *cstr) {
    std::string str(cstr);
    if (str == "1" || str == "true") {
        return true;
    }
    if (str == "0" || str == "false") {
        return false;
    }
    assert(false);
}

/**
 * Handle optional CLI arguments
 * https://stackoverflow.com/a/69177115
 */
#define LOST_OPTIONAL_OPTARG()                                   \
    ((optarg == NULL && optind < argc && argv[optind][0] != '-') \
     ? (bool) (optarg = argv[optind++])                          \
     : (optarg != NULL))

// This is separate from `main` just because it's in the `lost` namespace
static int LostMain(int argc, char **argv) {

    if (argc == 1) {
        std::cout << "Usage: ./lost database or ./lost pipeline" << std::endl
                  << "Use --help flag on those commands for further help" << std::endl;
        return 0;
    }

    std::string command(argv[1]);
    optind = 2;

    if (command == "database") {

        enum class DatabaseCliOption {
#define LOST_CLI_OPTION(name, type, prop, defaultVal, converter, defaultArg) prop,
#include "database-options.hpp"
#undef LOST_CLI_OPTION
            help
        };

        static struct option long_options[] = {
#define LOST_CLI_OPTION(name, type, prop, defaultVal, converter, defaultArg) \
            {name,                                                      \
             defaultArg == 0 ? required_argument : optional_argument, \
             0,                                                         \
             (int)DatabaseCliOption::prop},
#include "database-options.hpp" // NOLINT
#undef LOST_CLI_OPTION
                {"help", no_argument, 0, (int) DatabaseCliOption::help},
                {0}
        };

        DatabaseOptions databaseOptions;
        int index;
        int option;

        while ((option = getopt_long(argc, argv, "", long_options, &index)) != -1) {
            switch (option) {
#define LOST_CLI_OPTION(name, type, prop, defaultVal, converter, defaultArg) \
                case (int)DatabaseCliOption::prop :                     \
                    if (defaultArg == 0) {     \
                        databaseOptions.prop = converter;       \
                    } else {                                    \
                        if (LOST_OPTIONAL_OPTARG()) {           \
                            databaseOptions.prop = converter;   \
                        } else {                                \
                            databaseOptions.prop = defaultArg;  \
                        }                                       \
                    }                                           \
            break;
#include "database-options.hpp" // NOLINT
#undef LOST_CLI_OPTION
                case (int) DatabaseCliOption::help :std::cout << documentation_database_txt << std::endl;
                    return 0;
                    break;
                default :std::cout << "Illegal flag" << std::endl;
                    exit(1);
            }
        }

        lost::DatabaseBuild(databaseOptions);

    } else if (command == "pipeline") {

        enum class PipelineCliOption {
#define LOST_CLI_OPTION(name, type, prop, defaultVal, converter, defaultArg) prop,
#include "pipeline-options.hpp"
#undef LOST_CLI_OPTION
            help
        };

        static struct option long_options[] = {
#define LOST_CLI_OPTION(name, type, prop, defaultVal, converter, defaultArg) \
            {name,                                                      \
             defaultArg == 0 ? required_argument : optional_argument, \
             0,                                                         \
             (int)PipelineCliOption::prop},
#include "pipeline-options.hpp" // NOLINT
#undef LOST_CLI_OPTION

                // DATABASES
                {"help", no_argument, 0, (int) PipelineCliOption::help},
                {0, 0, 0, 0}
        };

        lost::PipelineOptions pipelineOptions;
        int index;
        int option;

        while ((option = getopt_long(argc, argv, "", long_options, &index)) != -1) {
            switch (option) {
#define LOST_CLI_OPTION(name, type, prop, defaultVal, converter, defaultArg) \
                case (int)PipelineCliOption::prop :                         \
                    if (defaultArg == 0) {    \
                        pipelineOptions.prop = converter;       \
                    } else {                                    \
                        if (LOST_OPTIONAL_OPTARG()) {           \
                            pipelineOptions.prop = converter;   \
                        } else {                                \
                            pipelineOptions.prop = defaultArg;  \
                        }                                       \
                    }                                           \
            break;
#include "pipeline-options.hpp" // NOLINT
#undef LOST_CLI_OPTION
                case (int) PipelineCliOption::help :std::cout << documentation_pipeline_txt << std::endl;
                    return 0;
                    break;
                default :std::cout << "Illegal flag" << std::endl;
                    exit(1);
            }
        }

        lost::PipelineRun(pipelineOptions);

    } else if (command == "sfml") {
        std::cout << "SFML command invoked" << "\n";

        enum class PipelineCliOption {
            #define LOST_CLI_OPTION(name, type, prop, defaultVal, converter, defaultArg) prop,
            #include "pipeline-options.hpp"
            #undef LOST_CLI_OPTION
                        help
        };


        static struct option long_options[] = {
            #define LOST_CLI_OPTION(name, type, prop, defaultVal, converter, defaultArg) \
                        {name,                                                      \
                        defaultArg == 0 ? required_argument : optional_argument, \
                        0,                                                         \
                        (int)PipelineCliOption::prop},
            #include "pipeline-options.hpp" // NOLINT
            #undef LOST_CLI_OPTION

                            // DATABASES
                            {"help", no_argument, 0, (int) PipelineCliOption::help},
                            {0, 0, 0, 0}
        };




        lost::PipelineOptions pipelineOptions;
        int index;
        int option;

        while ((option = getopt_long(argc, argv, "", long_options, &index)) != -1) {
            switch (option) {
                #define LOST_CLI_OPTION(name, type, prop, defaultVal, converter, defaultArg) \
                case (int)PipelineCliOption::prop :                         \
                    if (defaultArg == 0) {    \
                        pipelineOptions.prop = converter;       \
                        } else {                                    \
                            if (LOST_OPTIONAL_OPTARG()) {           \
                                pipelineOptions.prop = converter;   \
                            } else {                                \
                                pipelineOptions.prop = defaultArg;  \
                            }                                       \
                        }                                           \
                break;


                #include "pipeline-options.hpp" // NOLINT
                #undef LOST_CLI_OPTION
                case (int) PipelineCliOption::help :std::cout << documentation_pipeline_txt << std::endl;
                        return 0;
                        break;
                    default :std::cout << "Illegal flag" << std::endl;
                        exit(1);
            }

            // print option
            std::cout << option << " b " << "\n";
        }

        pipelineOptions.panning = false;

        std::vector<dost_ImgData> imgData = lost::PipelineRunSFML(pipelineOptions);
        
        // Initiate window and frame image holders.
        sf::RenderWindow window(sf::VideoMode(1024, 1024), "LOST Animation");


        // Hold textures in deque to prevent invalidation on push_back
        std::deque<sf::Texture> textures;
        std::vector<sf::Sprite> sprites;

        sprites.reserve(pipelineOptions.frames);

        // Load images.
        for (int frame = 0; frame < pipelineOptions.frames; frame++) {
            char buffer[256];
            snprintf(buffer, sizeof(buffer), "sfml-tests/frame_%04d.png", frame);

            sf::Texture tex;
            if (!tex.loadFromFile(buffer)) {
                std::cerr << "Failed to load " << buffer << "\n";
                continue;
            }

            textures.push_back(tex);              
            sprites.emplace_back();               
            sprites.back().setTexture(textures.back());
        }

        int image_idx = 0;

        // Center each sprite
        for (auto& spr : sprites) {
            sf::FloatRect r = spr.getLocalBounds();
            spr.setOrigin(r.width / 2, r.height / 2);
            spr.setPosition(512, 512);
        }

            
        sf::Font font;
        if (!font.loadFromFile("arial.ttf")) { 
            std::cerr << "Failed to load font (place arial.ttf or other .ttf in the working directory)\n";
            return 1;
        }

        sf::Text text;
        text.setFont(font);
        text.setString("Attitude is UNKNOWN");


        auto UpdateHUD = [&](int idx) {
                if (imgData[idx].attitude.IsKnown()) {
                    EulerAngles s = imgData[idx].attitude.ToSpherical();
                    text.setString(
                        "RA: " + std::to_string(RadToDeg(s.ra)) +
                        " DE: " + std::to_string(RadToDeg(s.de)) +
                        " Roll: " + std::to_string(RadToDeg(s.roll))
                    );
                } else {
                    text.setString("Attitude is UNKNOWN");
                }
            };


        UpdateHUD(image_idx);

        std::vector<int> starToCatalogIndex;

        sfml::UpdateStarCatalogMapping(imgData[image_idx], starToCatalogIndex);

        decimal ra = pipelineOptions.generateRa;
        decimal de = pipelineOptions.generateDe;
        decimal roll = pipelineOptions.generateRoll;

        auto starsNames = sfml::loadStarNames("starnames.csv");

        text.setCharacterSize(24);        
        text.setFillColor(sf::Color::Green); 

        const float margin = 6.f;
        text.setPosition(margin, margin);


        
        // --------------------------------------
        // Main loop
        // --------------------------------------
        while (window.isOpen())
        {
            sf::Event event;
            while (window.pollEvent(event))
            {
                if (event.type == sf::Event::Closed)
                    window.close();

                if (event.type == sf::Event::KeyPressed)
                {
                    if (event.key.code == sf::Keyboard::Right) {
                        image_idx = (image_idx + 1) % sprites.size();     // forward wrap

                        UpdateHUD(image_idx);


                        sfml::UpdateStarCatalogMapping(imgData[image_idx], starToCatalogIndex);


                    }
                    if (event.key.code == sf::Keyboard::Left) {
                        image_idx = (image_idx - 1 + sprites.size()) % sprites.size(); // backward wrap

                        UpdateHUD(image_idx);


                        sfml::UpdateStarCatalogMapping(imgData[image_idx], starToCatalogIndex);
                    }


                    // what im about to do is TERRIBLE. REIMPLEMENT! THIS IS FOR TESTING!!!!!

                    if (event.key.code == sf::Keyboard::A || event.key.code == sf::Keyboard::D ||
                        event.key.code == sf::Keyboard::W || event.key.code == sf::Keyboard::S ||
                        event.key.code == sf::Keyboard::Q || event.key.code == sf::Keyboard::E) {

                        if (image_idx < (int)sprites.size() - 1) {
                            int newSize = image_idx + 1;
                            
                            
                            sprites.resize(newSize);
                            textures.resize(newSize);
                            imgData.resize(newSize);
                            
                            
                            pipelineOptions.frames = newSize;


                            if (imgData[image_idx].attitude.IsKnown()) {
                                EulerAngles s = imgData[image_idx].attitude.ToSpherical();
                                ra = RadToDeg(s.ra);
                                de = RadToDeg(s.de);
                                roll = RadToDeg(s.roll);
                            }
                        }

                        // ra should clip to be within 0-360
                        ra -= 2.0f*(event.key.code == sf::Keyboard::A ? -1.0f : 0.0f) + 2.0f*(event.key.code == sf::Keyboard::D ? 1.0f : 0.0f);
                        if (ra > 360.0f) ra -= 360.0f;
                        if (ra < 0.0f) ra += 360.0f;
                        de += 2.0f*(event.key.code == sf::Keyboard::W ? 1.0f : 0.0f) + 2.0f*(event.key.code == sf::Keyboard::S ? -1.0f : 0.0f);
                        if (de > 90.0f) de = 90.0f;
                        if (de < -90.0f) de = -90.0f;
                        roll += 5.0f*(event.key.code == sf::Keyboard::Q ? -1.0f : 0.0f) + 5.0f*(event.key.code == sf::Keyboard::E ? 1.0f : 0.0f);
                        if (roll > 360.0f) roll -= 360.0f;
                        if (roll < 0.0f) roll += 360.0f;

                        // could  shave line off by updating this directly.
                        pipelineOptions.generateRa = ra;
                        pipelineOptions.raMin = ra;
                        pipelineOptions.raMax = ra;
                        pipelineOptions.generateDe = de;
                        pipelineOptions.decMin = de;
                        pipelineOptions.decMax = de;
                        pipelineOptions.generateRoll = roll;
                        pipelineOptions.rollMin = roll;
                        pipelineOptions.rollMax = roll;
                        pipelineOptions.frames += 1;
                        pipelineOptions.panning = true;


                        std::vector<dost_ImgData> imgDataTemp = lost::PipelineRunSFML(pipelineOptions);

                        imgData.push_back(imgDataTemp[0]);

                        char buffer[256];

                        snprintf(buffer, sizeof(buffer), "sfml-tests/frame_%04zu.png", sprites.size()); // silly naming conventions


                        sf::Texture tex;
                        if (!tex.loadFromFile(buffer)) {
                            std::cerr << "Failed to load " << buffer << "\n";
                            continue;
                        }

                        textures.push_back(tex);              // copy or move
                        sprites.emplace_back();               // default sprite
                        sprites.back().setTexture(textures.back());

                        /// OLDDD

                        image_idx = sprites.size()-1;     // we need to go forward to new image.



                        UpdateHUD(image_idx);

                        sfml::UpdateStarCatalogMapping(imgData[image_idx], starToCatalogIndex);
                    }

                }

            }

            //display text in the top left of current attitude

            window.clear();




            window.draw(sprites[image_idx]);
            window.draw(text);

            auto& stars = imgData[image_idx].stars;
            auto& starIds = imgData[image_idx].starIds;

            sf::Vector2f sum(0.f, 0.f);
            int count = 0;

            for (std::pair<int,int> id : starIds) {
                if (id.first >= 0 && id.first < (int)stars.size()) {
                    sum.x += stars[id.first].position.x;
                    sum.y += stars[id.first].position.y;
                    count++;
                }
            }

            sf::Vector2f center;

            if (count > 0) { // A center exists
                center = sf::Vector2f(sum.x / count, sum.y / count);
            }


            // i wanna see if there is a more efficient way to do this

            for (size_t i = 0; i < stars.size(); i++) {
                Star& star = stars[i];

                // pair with .first as starIndex, .second as catalogIndex we care about indexing with first
                //bool isMatched = (std::find(starIds.begin(), starIds.end(), std::make_pair(i, 0)) != starIds.end());
                

                int pairindex = starToCatalogIndex[i];

                // Draw box
                sf::RectangleShape box = sfml::CreateStarBox(star, pairindex != -1);
                window.draw(box);


                if (pairindex != -1 && count > 0) { // A center exists
                    sf::Vertex line[] = {
                        sf::Vertex(center, sf::Color::Cyan),
                        sf::Vertex(sf::Vector2f(star.position.x, star.position.y), sf::Color::Cyan)};

                    // Draw star label
                    sf::Text starText = sfml::CreateStarLabel(star, pairindex, starsNames, font);
                    
                    window.draw(starText);
                    window.draw(line, 2, sf::Lines);
                }
            }

            window.display();


            

            sf::sleep(sf::milliseconds(32));
        }
    } else {
        std::cout << "Usage: ./lost database or ./lost pipeline" << std::endl
                  << "Use --help flag on those commands for further help" << std::endl;
    }
    return 0;
}

}

int main(int argc, char **argv) {
    return lost::LostMain(argc, argv);
}