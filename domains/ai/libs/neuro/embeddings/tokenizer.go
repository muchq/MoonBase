package embeddings

import (
	"fmt"
	"strings"
	"unicode"
)

type Tokenizer interface {
	Tokenize(text string) []string
	Encode(tokens []string) []int
	Decode(ids []int) []string
	VocabSize() int
}

type SimpleTokenizer struct {
	vocab        map[string]int
	reverseVocab map[int]string
	lowercase    bool
	stopWords    map[string]bool
	specialTokens map[string]int
}

func NewSimpleTokenizer(lowercase bool) *SimpleTokenizer {
	t := &SimpleTokenizer{
		vocab:        make(map[string]int),
		reverseVocab: make(map[int]string),
		lowercase:    lowercase,
		stopWords:    make(map[string]bool),
		specialTokens: map[string]int{
			"[PAD]": 0,
			"[UNK]": 1,
			"[CLS]": 2,
			"[SEP]": 3,
			"[MASK]": 4,
		},
	}
	
	for token, id := range t.specialTokens {
		t.vocab[token] = id
		t.reverseVocab[id] = token
	}
	
	t.initializeCommonStopWords()
	
	return t
}

func (t *SimpleTokenizer) initializeCommonStopWords() {
	stopWords := []string{
		"the", "a", "an", "and", "or", "but", "in", "on", "at", "to", "for",
		"of", "with", "by", "from", "as", "is", "was", "are", "were", "been",
	}
	for _, word := range stopWords {
		t.stopWords[word] = true
	}
}

func (t *SimpleTokenizer) Tokenize(text string) []string {
	if t.lowercase {
		text = strings.ToLower(text)
	}
	
	var tokens []string
	var currentToken strings.Builder
	
	for _, r := range text {
		if unicode.IsSpace(r) || unicode.IsPunct(r) {
			if currentToken.Len() > 0 {
				token := currentToken.String()
				if !t.stopWords[token] {
					tokens = append(tokens, token)
				}
				currentToken.Reset()
			}
			if unicode.IsPunct(r) && r != '\'' {
				tokens = append(tokens, string(r))
			}
		} else {
			currentToken.WriteRune(r)
		}
	}
	
	if currentToken.Len() > 0 {
		token := currentToken.String()
		if !t.stopWords[token] {
			tokens = append(tokens, token)
		}
	}
	
	return tokens
}

func (t *SimpleTokenizer) Encode(tokens []string) []int {
	ids := make([]int, len(tokens))
	nextID := len(t.vocab)
	
	for i, token := range tokens {
		if id, exists := t.vocab[token]; exists {
			ids[i] = id
		} else {
			t.vocab[token] = nextID
			t.reverseVocab[nextID] = token
			ids[i] = nextID
			nextID++
		}
	}
	
	return ids
}

func (t *SimpleTokenizer) Decode(ids []int) []string {
	tokens := make([]string, len(ids))
	
	for i, id := range ids {
		if token, exists := t.reverseVocab[id]; exists {
			tokens[i] = token
		} else {
			tokens[i] = "[UNK]"
		}
	}
	
	return tokens
}

func (t *SimpleTokenizer) VocabSize() int {
	return len(t.vocab)
}

type WordPieceTokenizer struct {
	vocab         map[string]int
	reverseVocab  map[int]string
	maxInputChars int
	unkToken      string
	specialTokens map[string]int
}

func NewWordPieceTokenizer(vocabPath string) (*WordPieceTokenizer, error) {
	t := &WordPieceTokenizer{
		vocab:         make(map[string]int),
		reverseVocab:  make(map[int]string),
		maxInputChars: 512,
		unkToken:      "[UNK]",
		specialTokens: map[string]int{
			"[PAD]": 0,
			"[UNK]": 1,
			"[CLS]": 2,
			"[SEP]": 3,
			"[MASK]": 4,
		},
	}
	
	for token, id := range t.specialTokens {
		t.vocab[token] = id
		t.reverseVocab[id] = token
	}
	
	if vocabPath != "" {
		if err := t.loadVocabulary(vocabPath); err != nil {
			return nil, fmt.Errorf("failed to load vocabulary: %w", err)
		}
	} else {
		t.initializeBasicVocab()
	}
	
	return t, nil
}

func (t *WordPieceTokenizer) loadVocabulary(path string) error {
	return fmt.Errorf("vocabulary loading not yet implemented")
}

func (t *WordPieceTokenizer) initializeBasicVocab() {
	basicTokens := []string{
		"##ing", "##ed", "##er", "##est", "##ly", "##tion", "##ness",
		"##ment", "##ful", "##less", "##able", "##ive", "##ous", "##al",
	}
	
	nextID := len(t.vocab)
	for _, token := range basicTokens {
		t.vocab[token] = nextID
		t.reverseVocab[nextID] = token
		nextID++
	}
}

func (t *WordPieceTokenizer) Tokenize(text string) []string {
	text = strings.ToLower(text)
	words := strings.Fields(text)
	
	var outputTokens []string
	
	for _, word := range words {
		if len(word) > t.maxInputChars {
			outputTokens = append(outputTokens, t.unkToken)
			continue
		}
		
		tokens := t.wordPieceTokenize(word)
		outputTokens = append(outputTokens, tokens...)
	}
	
	return outputTokens
}

func (t *WordPieceTokenizer) wordPieceTokenize(word string) []string {
	if _, exists := t.vocab[word]; exists {
		return []string{word}
	}
	
	var tokens []string
	start := 0
	
	for start < len(word) {
		end := len(word)
		var curStr string
		found := false
		
		for start < end {
			substr := word[start:end]
			if start > 0 {
				substr = "##" + substr
			}
			
			if _, exists := t.vocab[substr]; exists {
				curStr = substr
				found = true
				break
			}
			end--
		}
		
		if !found {
			tokens = append(tokens, t.unkToken)
			break
		}
		
		tokens = append(tokens, curStr)
		start = end
	}
	
	return tokens
}

func (t *WordPieceTokenizer) Encode(tokens []string) []int {
	ids := make([]int, len(tokens))
	nextID := len(t.vocab)
	
	for i, token := range tokens {
		if id, exists := t.vocab[token]; exists {
			ids[i] = id
		} else {
			t.vocab[token] = nextID
			t.reverseVocab[nextID] = token
			ids[i] = nextID
			nextID++
		}
	}
	
	return ids
}

func (t *WordPieceTokenizer) Decode(ids []int) []string {
	tokens := make([]string, len(ids))
	
	for i, id := range ids {
		if token, exists := t.reverseVocab[id]; exists {
			tokens[i] = token
		} else {
			tokens[i] = t.unkToken
		}
	}
	
	return tokens
}

func (t *WordPieceTokenizer) VocabSize() int {
	return len(t.vocab)
}

func (t *WordPieceTokenizer) AddSpecialTokens(tokens []string) []string {
	result := make([]string, 0, len(tokens)+2)
	result = append(result, "[CLS]")
	result = append(result, tokens...)
	result = append(result, "[SEP]")
	return result
}

func (t *WordPieceTokenizer) GetPaddingToken() string {
	return "[PAD]"
}

func (t *WordPieceTokenizer) GetPaddingID() int {
	return t.specialTokens["[PAD]"]
}