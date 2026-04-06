/*
	This is a comment
*/
SELECT
	Book.title AS Title,
	count(*) AS Authors
FROM  Book
JOIN  Book_author
  ON  Book.isbn = Book_author.isbn
GROUP BY Book.title;
