#ifndef SFML_UTILS_H
#define SFML_UTILS_H

#include <SFML/Graphics.hpp>
#include "star-id.hpp"
#include "star-utils.hpp"

enum class TextAlign { Left, Right, Center };

namespace sfml {
    sf::Text CreateText(const std::string& str, const sf::Font& font, unsigned int size, sf::Color color, sf::Vector2f position);

    sf::Text CreateStarLabel(const lost::Star& star, int index, const std::vector<std::string>& names, const sf::Font& font);

    sf::RectangleShape CreateStarBox(const lost::Star& star, bool isMatched);
}

#endif
