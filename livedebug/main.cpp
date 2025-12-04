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


#include "databases.hpp"
#include "centroiders.hpp"
#include "decimal.hpp"
#include "io.hpp"
#include "man-database.h"
#include "man-pipeline.h"
#include "star-id.hpp"
#include "star-utils.hpp"


#include <SFML/Graphics.hpp>


// Used to store frame data.
struct dost_ImgData {
    // vector of stars
    // attitude
    lost::Attitude attitude;
    std::vector<lost::Star> stars; 
    std::vector<std::pair<int,int>> starIds;
};



// Names are not properly aligned, don't worry about this for now.
std::vector<std::string> loadStarNames(const std::string& filename) {
    std::vector<std::string> names;
    std::ifstream file(filename);
    std::string line;

    
    std::getline(file, line);

    while (std::getline(file, line)) {
        
        if (!line.empty() && line.front() == '"' && line.back() == '"') {
            line = line.substr(1, line.size() - 2);
        }
        names.push_back(line);
    }

    return names;
}


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
    
    // Step Function Implmentation
    if (values.generate == 0) { 
        values.generate = 1;
    }

    if ((values.rollMin != 0 || values.raMin != 0 || values.decMin != 0)) {
        values.rollMax = values.rollMax == 0 ? values.rollMin : values.rollMax;
        values.raMax = values.raMax == 0 ? values.raMin : values.raMax;
        values.decMax = values.decMax == 0 ? values.decMin : values.decMax;


        for (int frame = 0; frame < values.frames; frame++) {
            std::cout << frame << "\n";

            // image data
            dost_ImgData imgData;

            if (!values.panning && values.frames > 1) {
                values.generateRoll = values.rollMin + frame * (values.rollMax - values.rollMin) / (values.frames - 1);
                values.generateRa = values.raMin + frame * (values.raMax - values.raMin) / (values.frames - 1);
                values.generateDe = values.decMin + frame * (values.decMax - values.decMin) / (values.frames - 1);

            } else {
                values.generateRoll = values.rollMin;
                values.generateRa = values.raMin;
                values.generateDe = values.decMin;


                if (values.panning) {
                    frame = values.frames;
                }
                
            }



            values.centroidAlgo = "cog";
            values.idAlgo = "py";
            values.attitudeAlgo = "dqm";
            values.databasePath = "my-database.dat";


            // file in /sfml-tests/frame_XXXX.png
            char buffer[256];
            snprintf(buffer, sizeof(buffer), "sfml-tests/frame_%04d.png", frame);
            char buffer2[256];
            snprintf(buffer2, sizeof(buffer2), "sfml-tests/frame_raw_%04d.png", frame);
            values.plotInput = std::string(buffer2);
            values.plotRawInput = std::string(buffer);


            PipelineInputList input = GetPipelineInput(values);
            Pipeline pipeline = SetPipeline(values);
            // print pipeline centroidalgorithm
            std::cout << "Centroid Algorithm: " << values.centroidAlgo << std::endl;

            std::vector<PipelineOutput> outputs = pipeline.Go(input);

            imgData.attitude = *(outputs[0].attitude);
            imgData.stars = *(outputs[0].stars);
 

            


            for (const PipelineOutput& output : outputs) {



                if (output.attitude && output.attitude->IsKnown()) {
                    EulerAngles spherical = output.attitude->ToSpherical();
                    std::cout << "RA: " << RadToDeg(spherical.ra)
                            << " DE: " << RadToDeg(spherical.de)
                            << " Roll: " << RadToDeg(spherical.roll) << "\n";
                } else {
                    std::cout << "Attitude is UNKNOWN\n";
                }


                if (!output.stars) {
                    std::cout << "no stars\n";
                    continue;
                }

                const Stars& stars = *(output.stars);
                std::cout << stars.size() << " stars:\n";

                for (const Star& star : stars) {
                    std::cout << "  star at (" 
                            << star.position.x << ", " 
                            << star.position.y << "), R=("
                            << star.radiusX << ", " << star.radiusY << "), mag="
                            << star.magnitude << "\n";
                }

                if (output.starIds && output.catalog.size() > 0) {
                    for (const StarIdentifier &id : *output.starIds) {
                        imgData.starIds.push_back(std::make_pair(id.starIndex, id.catalogIndex)); 
                        std::cout
                            << "starIndex=" << id.starIndex
                            << " catalogIndex=" << id.catalogIndex;

                        if (id.catalogIndex >= 0 && id.catalogIndex < (int)output.catalog.size()) {
                            const CatalogStar &cs = output.catalog[id.catalogIndex];
                            std::cout << " catalogName=" << cs.name
                                    << " magnitude=" << cs.magnitude;
                        } else {
                            std::cout << " catalogName=<invalid index>";
                        }

                        std::cout << " weight=" << id.weight << std::endl;
                    }
                } else {
                    std::cout << "No starIds available." << std::endl;
                }



                returnData.push_back(imgData);
            }


            PipelineComparison(input, outputs, values);

            

        }
    } else{
        PipelineInputList input = GetPipelineInput(values);
        Pipeline pipeline = SetPipeline(values);
        std::vector<PipelineOutput> outputs = pipeline.Go(input);
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
        


        sf::RenderWindow window(sf::VideoMode(1024, 1024), "LOST Animation");

        pipelineOptions.panning = false;

        std::vector<dost_ImgData> imgData = lost::PipelineRunSFML(pipelineOptions);

        std::vector<sf::Texture> textures;
        std::vector<sf::Sprite> sprites;

        textures.reserve(pipelineOptions.frames);
        sprites.reserve(pipelineOptions.frames);

        for (int frame = 0; frame < pipelineOptions.frames; frame++) {
            char buffer[256];
            snprintf(buffer, sizeof(buffer), "sfml-tests/frame_%04d.png", frame);

            sf::Texture tex;
            if (!tex.loadFromFile(buffer)) {
                std::cerr << "Failed to load " << buffer << "\n";
                continue;
            }

            textures.push_back(tex);              // copy or move
            sprites.emplace_back();               // default sprite
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

        if (imgData[image_idx].attitude.IsKnown()) {
            EulerAngles spherical = imgData[image_idx].attitude.ToSpherical();

            // setstring to attitude
            text.setString(
                "RA: " + std::to_string(RadToDeg(spherical.ra)) +
                " DE: " + std::to_string(RadToDeg(spherical.de)) +
                " Roll: " + std::to_string(RadToDeg(spherical.roll))
            );

        } else {
                std::cout << "Attitude is UNKNOWN";
        }

        decimal ra = pipelineOptions.generateRa;
        decimal de = pipelineOptions.generateDe;
        decimal roll = pipelineOptions.generateRoll;

        auto starsNames = loadStarNames("starnames.csv");



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

                        if (imgData[image_idx].attitude.IsKnown()) {
                            EulerAngles spherical = imgData[image_idx].attitude.ToSpherical();

                            // setstring to attitude
                            text.setString(
                                "RA: " + std::to_string(RadToDeg(spherical.ra)) +
                                " DE: " + std::to_string(RadToDeg(spherical.de)) +
                                " Roll: " + std::to_string(RadToDeg(spherical.roll))
                            );

                        } else {
                            text.setString("Attitude is UNKNOWN");
                        }


                    }
                    if (event.key.code == sf::Keyboard::Left) {
                        image_idx = (image_idx - 1 + sprites.size()) % sprites.size(); // backward wrap

                        if (imgData[image_idx].attitude.IsKnown()) {
                            EulerAngles spherical = imgData[image_idx].attitude.ToSpherical();

                            // setstring to attitude
                            text.setString(
                                "RA: " + std::to_string(RadToDeg(spherical.ra)) +
                                " DE: " + std::to_string(RadToDeg(spherical.de)) +
                                " Roll: " + std::to_string(RadToDeg(spherical.roll))
                            );

                        } else {
                            text.setString("Attitude is UNKNOWN");
                        }

                    }


                    // what im about to do is TERRIBLE. REIMPLEMENT! THIS IS FOR TESTING!!!!!

                    if (event.key.code == sf::Keyboard::A || event.key.code == sf::Keyboard::D ||
                        event.key.code == sf::Keyboard::W || event.key.code == sf::Keyboard::S ||
                        event.key.code == sf::Keyboard::Q || event.key.code == sf::Keyboard::E) {

                        // ra should clip to be within 0-360
                        ra -= 2.0f*(event.key.code == sf::Keyboard::A ? -1.0f : 0.0f) + 2.0f*(event.key.code == sf::Keyboard::D ? 1.0f : 0.0f);
                        if (ra > 360.0f) ra -= 360.0f;
                        if (ra < 0.0f) ra += 360.0f;
                        de -= 2.0f*(event.key.code == sf::Keyboard::W ? 1.0f : 0.0f) + 2.0f*(event.key.code == sf::Keyboard::S ? -1.0f : 0.0f);
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

                        snprintf(buffer, sizeof(buffer), "sfml-tests/frame_%04zu.png", sprites.size()+1); // silly naming conventions


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

                        if (imgData[image_idx].attitude.IsKnown()) {
                            EulerAngles spherical = imgData[image_idx].attitude.ToSpherical();

                            // setstring to attitude
                            text.setString(
                                "RA: " + std::to_string(RadToDeg(spherical.ra)) +
                                " DE: " + std::to_string(RadToDeg(spherical.de)) +
                                " Roll: " + std::to_string(RadToDeg(spherical.roll))
                            );

                        } else {
                            text.setString("Attitude is UNKNOWN");
                        }


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
            bool haveCenter = (count > 0);

            if (haveCenter) {
                center = sf::Vector2f(sum.x / count, sum.y / count);
            }

            for (size_t i = 0; i < stars.size(); i++) {
                Star& star = stars[i];

                // pair with .first as starIndex, .second as catalogIndex we care about indexing with first
                //bool isMatched = (std::find(starIds.begin(), starIds.end(), std::make_pair(i, 0)) != starIds.end());
                int pairindex = -1;

                bool isMatched = false;
                for (std::pair<int,int> id : starIds) {
                    if (id.first == (int)i) {
                        pairindex = id.second;
                        isMatched = true;
                        break;
                    }
                }

                // --- Draw box ---
                sf::RectangleShape box;
                box.setPosition(star.position.x - star.radiusX * 4,
                                star.position.y - star.radiusY * 4);
                box.setSize(sf::Vector2f(star.radiusX * 8, star.radiusY * 8));
                box.setFillColor(sf::Color::Transparent);
                box.setOutlineThickness(1);
                box.setOutlineColor(isMatched ? sf::Color::Green : sf::Color::Red);
                window.draw(box);


                if (isMatched && haveCenter) {
                    sf::Vertex line[] = {
                        sf::Vertex(center, sf::Color::Cyan),
                        sf::Vertex(sf::Vector2f(star.position.x, star.position.y), sf::Color::Cyan)
                    };
                    window.draw(line, 2, sf::Lines);

                    if (pairindex < 0) continue;

                    // draw starIds.catalogName near the star
                    sf::Text starText;
                    starText.setFont(font);
                    if (pairindex + 1 >= 0 && pairindex + 1 < (int)starsNames.size()) {
                        starText.setString(std::to_string(pairindex) + starsNames[pairindex + 1]);
                    } else {
                        starText.setString(std::to_string(pairindex) + " ?");
                    } //  Have not studied the database to show that the correct names will be used all the time. the +1 is to exclude Sol.
                    starText.setCharacterSize(18);
                    starText.setFillColor(sf::Color::White);
                    
                    // right most word should be on the top left corner of the star box
                    sf::FloatRect textRect = starText.getLocalBounds();
                    starText.setOrigin(textRect.width, 0);
                    starText.setPosition(star.position.x - star.radiusX * 8 -4, star.position.y - star.radiusY * 8 -4);
                    window.draw(starText);
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