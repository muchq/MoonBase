package golf

import (
	"errors"
	"strconv"
	"sync"
)

type GameStore interface {
	AddUser(userId string) error
	UserExists(userId string) (bool, error)
	RemoveUser(userId string) error
	GetUsers() (map[string]bool, error)
	NewGame(gameState GameState) (*GameState, error)
	ReadGame(gameId string) (*GameState, error)
	ReadGameByUserId(userId string) (*GameState, error)
	ReadAllGames() (map[*GameState]bool, error)
	UpdateGame(gameState *GameState) (*GameState, error)
}

type InMemoryGameStore struct {
	UsersOnline     map[string]bool
	GameIdsByUserId map[string]string
	GamesById       map[string]*GameState

	mu      sync.RWMutex
	counter int
}

func (s *InMemoryGameStore) AddUser(userId string) error {
	s.mu.Lock()
	defer s.mu.Unlock()

	if s.UsersOnline[userId] {
		return errors.New("user exists")
	}

	s.UsersOnline[userId] = true
	return nil
}

func (s *InMemoryGameStore) UserExists(userId string) (bool, error) {
	s.mu.RLock()
	defer s.mu.RUnlock()

	return s.UsersOnline[userId], nil
}

func (s *InMemoryGameStore) RemoveUser(userId string) error {
	s.mu.Lock()
	defer s.mu.Unlock()

	delete(s.UsersOnline, userId)
	return nil
}

func (s *InMemoryGameStore) GetUsers() (map[string]bool, error) {
	s.mu.RLock()
	defer s.mu.RUnlock()

	return s.UsersOnline, nil
}

func (s *InMemoryGameStore) NewGame(gameState *GameState) (*GameState, error) {
	s.mu.Lock()
	defer s.mu.Unlock()

	gameIdInt := s.counter
	s.counter += 1
	gameId := strconv.Itoa(gameIdInt)
	gameState.Id = gameId
	gameState.Version = "foo"
	if len(gameState.Players) == 0 {
		return nil, errors.New("new game requires a player")
	}

	playerId := gameState.Players[0].Id
	if s.GameIdsByUserId[playerId] != "" {
		return nil, errors.New("user already in game")
	}

	s.GamesById[gameId] = gameState
	s.GameIdsByUserId[playerId] = gameId
	return gameState, nil
}

func (s *InMemoryGameStore) ReadGame(gameId string) (*GameState, error) {
	s.mu.RLock()
	defer s.mu.RUnlock()

	game := s.GamesById[gameId]
	if game == nil {
		return nil, errors.New("game not found")
	}

	return game, nil
}

func (s *InMemoryGameStore) ReadGameByUserId(userId string) (*GameState, error) {
	s.mu.RLock()
	defer s.mu.RUnlock()

	gameId := s.GameIdsByUserId[userId]
	if gameId == "" {
		return nil, errors.New("game not found")
	}

	return s.GamesById[gameId], nil
}

func (s *InMemoryGameStore) UpdateGame(gameState *GameState) (*GameState, error) {
	s.mu.Lock()
	defer s.mu.Unlock()

	game := s.GamesById[gameState.Id]
	if game == nil {
		return nil, errors.New("game does not exist")
	}

	if game.IsOver() {
		return nil, errors.New("game is over")
	}

	for _, player := range game.Players {
		if player.Id != "" {
			s.GameIdsByUserId[player.Id] = game.Id
		}
	}

	s.GamesById[game.Id] = game
	return game, nil
}

func (s *InMemoryGameStore) ReadAllGames() (map[*GameState]bool, error) {
	games := make(map[*GameState]bool, len(s.GamesById))
	for _, game := range s.GamesById {
		games[game] = true
	}

	return games, nil
}
