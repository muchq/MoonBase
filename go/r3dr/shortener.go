package main

type Shortener struct {
	urlDao UrlDao
}

func NewShortener(urlDao UrlDao) *Shortener {
	return &Shortener{urlDao}
}

func (s *Shortener) Shorten(request ShortenRequest) (ShortenResponse, error) {
	slug, err := s.urlDao.InsertUrl(request.LongUrl, request.ExpiresAt)
	return ShortenResponse{Slug: slug}, err
}

func (s *Shortener) Redirect(slug string) (string, error) {
	return s.urlDao.GetLongUrl(slug)
}

func (s *Shortener) Close() {
	s.urlDao.Close()
}
