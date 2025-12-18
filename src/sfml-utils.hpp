#ifndef SFML_UTILS_H
#define SFML_UTILS_H

#include <SFML/Graphics.hpp>
#include "star-id.hpp"
#include "star-utils.hpp"
#include <fstream>


// Used to store frame data.
struct dost_ImgData {
    lost::Attitude attitude;
    std::vector<lost::Star> stars; 
    std::vector<std::pair<int,int>> starIds;
};


enum class TextAlign { Left, Right, Center };

namespace sfml {
    sf::Text CreateText(const std::string& str, const sf::Font& font, unsigned int size, sf::Color color, sf::Vector2f position);

    sf::Text CreateStarLabel(const lost::Star& star, int index, const std::vector<std::string>& names, const sf::Font& font);

    sf::RectangleShape CreateStarBox(const lost::Star& star, bool isMatched);

    std::vector<std::string> loadStarNames(const std::string& filename);


    void UpdateStarCatalogMapping(const dost_ImgData& currentFrame, std::vector<int>& outMapping);
}

#endif
