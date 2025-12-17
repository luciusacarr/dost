

#include <sfml-utils.hpp>

namespace sfml {

    sf::Text CreateText(const std::string& str, const sf::Font& font, unsigned int size, sf::Color color, sf::Vector2f position, TextAlign align) {
        sf::Text text;
        text.setFont(font);
        text.setString(str);
        text.setCharacterSize(size);
        text.setFillColor(color);

        
        sf::FloatRect bounds = text.getLocalBounds();
        if (align == TextAlign::Right) {
            text.setOrigin(bounds.width, 0);
        } else if (align == TextAlign::Center) {
            text.setOrigin(bounds.width / 2.f, bounds.height / 2.f);
        }
        

        text.setPosition(position);
        return text;
    }


    sf::Text CreateStarLabel(const lost::Star& star, int index, const std::vector<std::string>& names, const sf::Font& font) {
        
        if (index < 0) {
            return CreateText("?", font, 18, sf::Color::White, sf::Vector2f(star.position.x, star.position.y), TextAlign::Center);
        }

        std::string textStr = std::to_string(index);
        if (index + 1 >= 0 && index + 1 < (int)names.size()) 
            textStr += " " + names[index];
        else 
            textStr += " ?";

        
        float posX = star.position.x - star.radiusX * 8 - 4;
        float posY = star.position.y - star.radiusY * 8 - 4;

        
        sf::Text text;
        text.setFont(font);
        text.setString(textStr);
        text.setCharacterSize(18);
        text.setFillColor(sf::Color::White);
        
        // Right align logic
        text.setOrigin(text.getLocalBounds().width, 0); 
        text.setPosition(posX, posY);

        return text;
    }

    sf::RectangleShape CreateStarBox(const lost::Star& star, bool isMatched) {
        sf::RectangleShape box;
        box.setPosition(star.position.x - star.radiusX * 4,
                        star.position.y - star.radiusY * 4);
        box.setSize(sf::Vector2f(star.radiusX * 8, star.radiusY * 8));
        box.setFillColor(sf::Color::Transparent);
        box.setOutlineThickness(1);
        box.setOutlineColor(isMatched ? sf::Color::Green : sf::Color::Red);
        return box;
    }
}
