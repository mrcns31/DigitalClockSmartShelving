void digitZero(int offset, uint32_t colour){
    faceFill(colour, (0 + offset), 27);
    faceFill(colour, (36 + offset), 27);
}

void digitOne(int offset, uint32_t colour){
    faceFill(colour, (0 + offset), 9);
    faceFill(colour, (36 + offset), 9);
}

void digitTwo(int offset, uint32_t colour){
    faceFill(colour, (0 + offset), 18);
    faceFill(colour, (27 + offset), 9);
    faceFill(colour, (45 + offset), 18);
}

void digitThree(int offset, uint32_t colour){
    faceFill(colour, (0 + offset), 18);
    faceFill(colour, (27 + offset), 27);
}

void digitFour(int offset, uint32_t colour){
    faceFill(colour, (0 + offset), 9);
    faceFill(colour, (18 + offset), 27);
}

void digitFive(int offset, uint32_t colour){
    faceFill(colour, (9 + offset), 45);
}

void digitSix(int offset, uint32_t colour){
    faceFill(colour, (9 + offset), 54);
}

void digitSeven(int offset, uint32_t colour){
    faceFill(colour, (0 + offset), 18);
    faceFill(colour, (36 + offset), 9);
}

void digitEight(int offset, uint32_t colour){
    faceFill(colour, (0 + offset), 63);
}

void digitNine(int offset, uint32_t colour){
    faceFill(colour, (0 + offset), 45);
}
