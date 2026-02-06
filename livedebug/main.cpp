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

#include <dirent.h>



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

    // Force certain algorithms for current testing purposes.
    values.centroidAlgo = "cog";
    values.idAlgo = "py";
    values.attitudeAlgo = "dqm";
    values.databasePath = "my-database.dat";

    // Set up Pipeline
    Pipeline pipeline = SetPipeline(values);

    if (values.imageDir != "") {

        // have images to test? no generation needed! feed the directory in to test a batch. ill make it so you can pass in one png later...

        std::cout << "Processing images from: " << values.imageDir << std::endl;
        values.generate = 0;
        values.frames = 0;

        DIR *dir;
        struct dirent *ent;

        // Try to open the directory
        if ((dir = opendir(values.imageDir.c_str())) != NULL) {
            
            // Loop through all files in the directory
            while ((ent = readdir(dir)) != NULL) {
                values.frames += 1; // count files for info
                std::string filename = ent->d_name;

                // Skip "." and ".." directories
                if (filename == "." || filename == "..") continue;

                // Check for .png extension manually
                if (filename.length() >= 4 && 
                    filename.substr(filename.length() - 4) == ".png") {

                    // Construct full path: directory + / + filename
                    values.png = values.imageDir + "/" + filename;
                    
                    std::cout << "Loading: " << values.png << std::endl;

                    // Run Pipeline
                    PipelineInputList input = GetPipelineInput(values);
                    std::vector<PipelineOutput> outputs = pipeline.Go(input);

                    if (!outputs.empty()) {
                        const auto& out = outputs[0];
                        dost_ImgData imgData;

                        if (out.attitude) imgData.attitude = *out.attitude;
                        if (out.stars)    imgData.stars    = *out.stars;

                        if (out.starIds && !out.catalog.empty()) {
                            for (const StarIdentifier &id : *out.starIds) {
                                imgData.starIds.emplace_back(id.starIndex, id.catalogIndex);
                            }
                        }

                        imgData.resx = input[0]->InputImage()->width;
                        imgData.resy = input[0]->InputImage()->height;

                        // No truth data in image mode
                        imgData.trueRa   = 0;
                        imgData.trueDec  = 0;
                        imgData.trueRoll = 0;

                        imgData.trackedStars = out.trackedStars;

                        returnData.push_back(imgData);

                        PipelineComparison(input, outputs, values);
                    }
                }
            }
            closedir(dir);
        } else {
            std::cerr << "Error: Could not open directory " << values.imageDir << std::endl;
        }

    } else {

        // generate mode, standard!

        values.generate = 1;

        if (values.frames < 1) values.frames = 1;
        if (values.rollMax == 0) values.rollMax = values.rollMin;
        if (values.raMax == 0)   values.raMax   = values.raMin;
        if (values.decMax == 0)  values.decMax  = values.decMin;

        

        if (values.regenFalseDb) {
            std::cout << "Regenerating fake star database..." << std::endl;
            UpdateFakeStarsFile("fakestars.tsv", values.generateFalseMinMag, values.generateFalseMaxMag);
        }

        returnData.reserve(values.panning ? 1 : values.frames);

        int startFrame = 0;
        if (values.panning) startFrame = values.frames - 1;

        for (int frame = startFrame; frame < values.frames; frame++) {
            std::cout << "Processing frame: " << frame << "\n";

            double t = (values.frames > 1) ? (double)frame / (values.frames - 1) : 0.0;

            values.generateRoll = values.rollMin + t * (values.rollMax - values.rollMin);
            values.generateRa   = values.raMin   + t * (values.raMax   - values.raMin);
            values.generateDe   = values.decMin  + t * (values.decMax  - values.decMin);

            char buffer[256];
            snprintf(buffer, sizeof(buffer), "sfml-tests/frame_%04d.png", frame);
            values.plotRawInput = std::string(buffer);

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

            imgData.trueRa   = values.generateRa;
            imgData.trueDec  = values.generateDe;
            imgData.trueRoll = values.generateRoll;

            imgData.resx = values.generateXRes;
            imgData.resy = values.generateYRes;

            imgData.trackedStars = out.trackedStars;

            returnData.push_back(imgData);
            PipelineComparison(input, outputs, values); 
        }
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

struct TextureInfo {
    sf::Texture texture;
    float scaleFactor; // 1.0 = Original, 0.5 = Half Size, etc.
};

TextureInfo LoadTextureSafe(const std::string& filepath) {
    sf::Image rawImage;
    if (!rawImage.loadFromFile(filepath)) {
        std::cerr << "Error: File not found: " << filepath << std::endl;
        return {sf::Texture(), 1.0f};
    }

    unsigned int maxDim = sf::Texture::getMaximumSize(); // e.g. 4096
    sf::Vector2u imgSize = rawImage.getSize();

    // If image is within limits, load normally
    if (imgSize.x <= maxDim && imgSize.y <= maxDim) {
        sf::Texture tex;
        tex.loadFromImage(rawImage);
        return {tex, 1.0f};
    }

    // --- DOWNSCALING LOGIC ---
    // Calculate how much we need to shrink to fit GPU
    float scale = (float)maxDim / std::max(imgSize.x, imgSize.y);
    unsigned int newW = (unsigned int)(imgSize.x * scale);
    unsigned int newH = (unsigned int)(imgSize.y * scale);
    
    std::cout << "[GPU LIMIT] Downscaling " << imgSize.x << "x" << imgSize.y 
              << " -> " << newW << "x" << newH << " (Scale: " << scale << ")" << std::endl;

    // Create resized image (Nearest Neighbor is fast & preserves star crispness)
    sf::Image resized;
    resized.create(newW, newH);
    const uint8_t* srcPixels = rawImage.getPixelsPtr();
    
    // Simple pixel mapping loop
    for (unsigned int y = 0; y < newH; y++) {
        for (unsigned int x = 0; x < newW; x++) {
            int srcX = (int)(x / scale);
            int srcY = (int)(y / scale);
            resized.setPixel(x, y, rawImage.getPixel(srcX, srcY));
        }
    }

    sf::Texture tex;
    tex.loadFromImage(resized);
    return {tex, scale};
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



        std::vector<dost_ImgData> imgData = lost::PipelineRunSFML(pipelineOptions);

        pipelineOptions.regenFalseDb = false; // disable regen for WASDQE panning.
        

        // --------------------------------------
        // SFML Setup
        // --------------------------------------
        // Initiate window and frame image holders.

        // check the resolution of a given photo. can we resize upon frame change? sfml limit testing!


        int maxResY = sf::VideoMode::getDesktopMode().height - 200;

        for (int i = 0; i < (int)imgData.size(); i++) {
            // FIX 1: Float cast to preserve aspect ratio
            float aspect = static_cast<float>(imgData[i].resx) / static_cast<float>(imgData[i].resy);
            
            int imgMaxResY = std::min(imgData[i].resy, maxResY);
            imgData[i].realResy = imgMaxResY;
            imgData[i].realResx = static_cast<int>(imgMaxResY * aspect);
        }

        sf::RenderWindow window(sf::VideoMode(imgData[0].realResx, imgData[0].realResy), "LOST Animation");

        

        // Hold textures in deque to prevent invalidation on push_back
        std::deque<sf::Texture> textures;
        std::vector<sf::Sprite> sprites;
        std::vector<float> scaleFactors;

        sprites.reserve(pipelineOptions.frames);

        bool showStarBoxes = true;

        std::vector<std::string> filePaths;

        if (!pipelineOptions.imageDir.empty()) {

            DIR *dir;
            struct dirent *ent;
            if ((dir = opendir(pipelineOptions.imageDir.c_str())) != NULL) {
                while ((ent = readdir(dir)) != NULL) {
                    std::string filename = ent->d_name;

                    if (filename == "." || filename == "..") continue;

 
                    if (filename.length() >= 4 && filename.substr(filename.length() - 4) == ".png") {
                        filePaths.push_back(pipelineOptions.imageDir + "/" + filename);
                    }
                }
                closedir(dir);
            }

        } else {

            for (int frame = 0; frame < pipelineOptions.frames; frame++) {
                char buffer[256];
                snprintf(buffer, sizeof(buffer), "sfml-tests/frame_%04d.png", frame);
                filePaths.push_back(std::string(buffer));
            }
        }


        // Load Images into Textures/Sprites


        for (const std::string& path : filePaths) {
            
            TextureInfo info = LoadTextureSafe(path);
            
            textures.push_back(info.texture);
            scaleFactors.push_back(info.scaleFactor); // Save the scale!

            sprites.emplace_back();
            sprites.back().setTexture(textures.back());
        }

        int image_idx = 0;


            
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

                        window.setSize(sf::Vector2u(imgData[image_idx].realResx, imgData[image_idx].realResy)); // resize window to fit new image

                        sfml::UpdateStarCatalogMapping(imgData[image_idx], starToCatalogIndex);


                    }
                    if (event.key.code == sf::Keyboard::Left) {
                        image_idx = (image_idx - 1 + sprites.size()) % sprites.size(); // backward wrap

                        UpdateHUD(image_idx);



                        window.setSize(sf::Vector2u(imgData[image_idx].realResx, imgData[image_idx].realResy)); // resize window to fit new imagere


                        sfml::UpdateStarCatalogMapping(imgData[image_idx], starToCatalogIndex);
                    }


                    // messy

                    if (event.key.code == sf::Keyboard::A || event.key.code == sf::Keyboard::D ||
                        event.key.code == sf::Keyboard::W || event.key.code == sf::Keyboard::S ||
                        event.key.code == sf::Keyboard::Q || event.key.code == sf::Keyboard::E) {

                        if (image_idx < (int)sprites.size() - 1) {
                            int newSize = image_idx + 1;
                            
                            sprites.resize(newSize);
                            textures.resize(newSize);
                            imgData.resize(newSize);
                            
                            pipelineOptions.frames = newSize;


                            pipelineOptions.raMax = imgData[image_idx].trueRa;
                            pipelineOptions.decMax = imgData[image_idx].trueDec;
                            pipelineOptions.rollMax = imgData[image_idx].trueRoll;

                        }

                        // Adjust max attitude based on keypresses, since we are modifying the last frame we only need to adjust max
                        pipelineOptions.raMax -= 2.0f*(event.key.code == sf::Keyboard::A ? -1.0f : 0.0f) + 2.0f*(event.key.code == sf::Keyboard::D ? 1.0f : 0.0f);
                        if (pipelineOptions.raMax > 360.0f) pipelineOptions.raMax -= 360.0f;
                        if (pipelineOptions.raMax < 0.0f) pipelineOptions.raMax += 360.0f;
                        pipelineOptions.decMax += 2.0f*(event.key.code == sf::Keyboard::W ? 1.0f : 0.0f) + 2.0f*(event.key.code == sf::Keyboard::S ? -1.0f : 0.0f);
                        if (pipelineOptions.decMax > 90.0f) pipelineOptions.decMax = 90.0f;
                        if (pipelineOptions.decMax < -90.0f) pipelineOptions.decMax = -90.0f;
                        pipelineOptions.rollMax += 5.0f*(event.key.code == sf::Keyboard::Q ? -1.0f : 0.0f) + 5.0f*(event.key.code == sf::Keyboard::E ? 1.0f : 0.0f);
                        if (pipelineOptions.rollMax > 360.0f) pipelineOptions.rollMax -= 360.0f;
                        if (pipelineOptions.rollMax < 0.0f) pipelineOptions.rollMax += 360.0f;

                        pipelineOptions.panning = true;
                        pipelineOptions.frames += 1;


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
                    else if (event.key.code == sf::Keyboard::J) {
                        showStarBoxes = !showStarBoxes;
                    }

                }

            }

            //display text in the top left of current attitude

            window.clear();


            sf::View worldView(sf::FloatRect(0.f, 0.f, (float)imgData[image_idx].resx, (float)imgData[image_idx].resy));
            window.setView(worldView);

            window.draw(sprites[image_idx]);

            float s = scaleFactors[image_idx];

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

                bool isMatched = (pairindex != -1);

                // Draw box
                if (showStarBoxes || isMatched) {
                    sf::RectangleShape box = sfml::CreateStarBox(star, pairindex != -1);
                    window.draw(box);
                }


                if (isMatched && count > 0) { // A center exists
                    sf::Vertex line[] = {
                        sf::Vertex(center, sf::Color::Cyan),
                        sf::Vertex(sf::Vector2f(star.position.x, star.position.y), sf::Color::Cyan)};

                    // Draw star label
                    
                    sf::Text starText = sfml::CreateStarLabel(star, pairindex, starsNames, font);
                    
                    window.draw(starText);
                    window.draw(line, 2, sf::Lines);
                }
            }

            for (const auto& trackedStar : imgData[image_idx].trackedStars) {
                sf::CircleShape circle(10.f);
                circle.setFillColor(sf::Color::Transparent);
                circle.setOutlineColor(sf::Color::Red);
                circle.setOutlineThickness(2.f);
                circle.setOrigin(10.f, 10.f); // center the circle on the star
                circle.setPosition(trackedStar.first, trackedStar.second);
                window.draw(circle);
            }

            window.setView(window.getDefaultView());
            window.draw(text);

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